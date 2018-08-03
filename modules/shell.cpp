/*
 * Copyright (C) 2004-2018 ZNC, see the NOTICE file for details.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <znc/FileUtils.h>
#include <znc/User.h>
#include <znc/znc.h>
#include <znc/ExecSock.h>

using std::vector;

// Forward Declaration
class CShellMod;
class CShellSock;

class CPartnerSock : public CZNCSock {
  public:
    CPartnerSock(CShellMod* pShellMod, CIRCNetwork* pNetwork)
        : CZNCSock(), m_pPartner(nullptr),
          m_pParent(pShellMod), m_pNetwork(pNetwork) {}

    ~CPartnerSock() override;

    bool IsOfNetwork(CIRCNetwork* pNet) const { return pNet == m_pNetwork; }
    bool IsOfModule(CShellMod* pParent) const { return pParent == m_pParent; }

    void Disconnected();
    void ReadLine(const CString& sData);

    CShellSock* m_pPartner;
    CShellMod* m_pParent;
    CIRCNetwork* m_pNetwork;
};

class CShellSock : public CExecSock {
  public:
    CShellSock(CShellMod* pShellMod, CClient* pClient,
               const CString& sExec, CPartnerSock *extrasock) : CExecSock() {
        EnableReadLine();
        m_pParent = pShellMod;
        m_pClient = pClient;
        m_pPartner = extrasock;

        extrasock->EnableReadLine();
        extrasock->m_pPartner = this;
 
        if (Execute(sExec, extrasock) == -1) {
            auto e = errno;
            ReadLine(t_f("Failed to execute: {1}")(strerror(e)));
            return;
        }

        // Get rid of that write fd, we aren't going to use it
        // (And clients expecting input will fail this way).
        close(GetWSock());
        SetWSock(open("/dev/null", O_WRONLY));
        close(extrasock->GetWSock());
        extrasock->SetWSock(open("/dev/null", O_WRONLY));
    }

    ~CShellSock() override {
        if (m_pPartner) m_pPartner->m_pPartner = nullptr;
    }

    // These next two function's bodies are at the bottom of the file since they
    // reference CShellMod
    void ReadLine(const CString& sData) override;
    void Disconnected() override;

    bool IsOfClient(CClient* pClient) const { return pClient == m_pClient; }
    bool IsOfModule(CShellMod* pParent) const { return pParent == m_pParent; }

    CShellMod* m_pParent;
    CPartnerSock* m_pPartner;
    CClient* m_pClient;
};

class CShellMod : public CModule {
  public:
    MODCONSTRUCTOR(CShellMod) { m_sPath = CZNC::Get().GetHomePath(); }

    ~CShellMod() override {
        vector<Csock*> vSocks = GetManager()->FindSocksByName(shellname);

        for (unsigned int a = 0; a < vSocks.size(); a++) {
            GetManager()->DelSockByAddr(vSocks[a]);
        }
    }

    bool OnLoad(const CString& sArgs, CString& sMessage) override {
#ifndef MOD_SHELL_ALLOW_EVERYONE
        if (!GetUser()->IsAdmin()) {
            sMessage = t_s("You must be admin to use the shell module");
            return false;
        }
#endif

        return true;
    }

    void OnModCommand(const CString& sLine) override {
        CString sCommand = sLine.Token(0);
        if (sCommand.Equals("cd")) {
            CString sArg = sLine.Token(1, true);
            CString sPath = CDir::ChangeDir(
                m_sPath,
                (sArg.empty() ? CString(CZNC::Get().GetHomePath()) : sArg),
                CZNC::Get().GetHomePath());
            CFile Dir(sPath);

            if (Dir.IsDir()) {
                m_sPath = sPath;
            } else if (Dir.Exists()) {
                PutShell("cd: not a directory [" + sPath + "]");
            } else {
                PutShell("cd: no such directory [" + sPath + "]");
            }

            PutShell("znc$");
        } else {
            RunCommand(sLine);
        }
    }

    void PutShell(const CString& sMsg) {
        CString sPath = m_sPath.Replace_n(" ", "_");
        CString sSource = ":" + GetModNick() + "!shell@" + sPath;
        CString sLine =
            sSource + " PRIVMSG " + GetClient()->GetNick() + " :" + sMsg;
        GetClient()->PutClient(sLine);
    }

    void RunCommand(const CString& sCommand) {
        CPartnerSock* css_server = new CPartnerSock(this, GetNetwork());
        CShellSock* css_client = new CShellSock(this, GetClient(),
            "cd " + m_sPath + " && " + sCommand, css_server);
        m_pManager->AddSock(css_client, shellname);
        m_pManager->AddSock(css_server, shellname);
    }

    void OnClientDisconnect() override {
        std::vector<Csock*> vDeadCommands;
        for (Csock* pSock : *GetManager()) {
            if (CShellSock* pSSock = dynamic_cast<CShellSock*>(pSock)) {
                if (pSSock->IsOfModule(this) &&
                    pSSock->IsOfClient(GetClient())) {
                    vDeadCommands.push_back(pSock);
                }
            }
        }
        for (Csock* pSock : vDeadCommands) {
            GetManager()->DelSockByAddr(pSock);
        }
    }

    void OnIRCDisconnected() override {
        std::vector<Csock*> vDeadSocks;
        for (Csock* pSock : *GetManager()) {
            if (CPartnerSock* pSSock = dynamic_cast<CPartnerSock*>(pSock)) {
                if (pSSock->IsOfModule(this) &&
                    pSSock->IsOfNetwork(GetNetwork())) {
                    vDeadSocks.push_back(pSock);
                }
            }
        }
        for (Csock* pSock : vDeadSocks) {
            GetManager()->DelSockByAddr(pSock);
        }
    }

  private:
    CString m_sPath;
    static const char * const shellname;

};
const char * const CShellMod::shellname = "SHELL";

void CShellSock::ReadLine(const CString& sData) {
    CString sLine(sData);
    sLine.TrimRight("\r\n");
    sLine.Replace("\t", "    ");
 
    m_pParent->SetClient(m_pClient);
    m_pParent->PutShell(sLine);
    m_pParent->SetClient(nullptr);
}

void CShellSock::Disconnected() {
    // If there is some incomplete line in the buffer, read it
    // (e.g. echo echo -n "hi" triggered this)
    CString& sBuffer = GetInternalReadBuffer();
    if (!sBuffer.empty()) ReadLine(sBuffer);

    m_pParent->SetClient(m_pClient);
    m_pParent->PutShell("znc$");
    m_pParent->SetClient(nullptr);
}

void CPartnerSock::ReadLine(const CString& sData) {
    CString sLine(sData);
    sLine.TrimRight("\r\n");
    sLine.Replace("\t", "    ");

    m_pParent->SetNetwork(m_pNetwork);
    m_pParent->PutIRC(sLine);
    if (m_pPartner) {
        m_pParent->SetClient(m_pPartner->m_pClient);
        m_pParent->PutShell("IRC <== " + sLine);
        m_pParent->SetClient(nullptr);
    }
    m_pParent->SetNetwork(nullptr);
}

void CPartnerSock::Disconnected() {
    // If there is some incomplete line in the buffer, read it
    // (e.g. echo echo -n "hi" triggered this)
    CString& sBuffer = GetInternalReadBuffer();
    if (!sBuffer.empty()) ReadLine(sBuffer);
}

CPartnerSock::~CPartnerSock() {
    if (m_pPartner) m_pPartner->m_pPartner = nullptr;
}

template <>
void TModInfo<CShellMod>(CModInfo& Info) {
    Info.SetWikiPage("shell");
}

#ifdef MOD_SHELL_ALLOW_EVERYONE
USERMODULEDEFS(CShellMod, t_s("Gives shell access"))
#else
USERMODULEDEFS(CShellMod,
               t_s("Gives shell access. Only ZNC admins can use it."))
#endif
