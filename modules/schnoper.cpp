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

#include <znc/User.h>
#include <znc/IRCNetwork.h>

class CSchnoper : public CModule
{
private:
    CString      m_sOperPassword;
    CString      m_sOperUser;
    CString      m_sSnoMask;
    bool         m_bAutoOper;

public:
    MODCONSTRUCTOR(CSchnoper) {
        AddHelpCommand();
        AddCommand(
            "Forget", "",
            t_d("Forgets a cached oper username and password, if one exists."),
            [=](const CString& sLine) { ForgetCommand(sLine); });
        AddCommand("Oper", "",
                   t_d("Authenticate now."),
                   [=](const CString& sLine) {OperCommand(sLine); });
        m_sOperPassword  = "";
        m_sOperUser      = "";
        m_sSnoMask       = "";
        m_bAutoOper      = false;
    }

    ~CSchnoper() override {}

    bool OnLoad(const CString& sArgs, CString& sMessage) {
        if (GetType() != CModInfo::NetworkModule) {
            sMessage = t_s("You can only load this module in network mode.");
            return false;
        }
        return true;
    }

    void ForgetCommand(const CString& sLine) {
        for (size_t i = 0; i < m_sOperUser.length(); ++i) {
            m_sOperUser[i] = '\0';
        }
        for (size_t i = 0; i < m_sOperPassword.length(); ++i) {
            m_sOperPassword[i] = '\0';
        }
        m_sOperUser.clear();
        m_sOperPassword.clear();
        m_sSnoMask.clear();
        m_bAutoOper = false;
        PutModule(t_s("Deleted stored credentials."));
    }

    void OperCommand(const CString& sLine) {
        if (m_bAutoOper && !m_sOperPassword.empty() && !m_sOperUser.empty()) {
            PutModule(t_s("Resending cached OPER line..."));
            PutIRC("OPER " + m_sOperUser + " :" + m_sOperPassword);
            if (GetNetwork() && !m_sSnoMask.empty()) {
                PutIRC("MODE " + GetNetwork()->GetNick() +
                       " +s :" + m_sSnoMask);
            }
        }
    }

    virtual void OnIRCConnected()
    {
        OperCommand("");
    }

    virtual EModRet OnUserRaw(CString &sLine)
    {
        // line of the form OPER user :password
        if (sLine.Token(0).AsUpper() == "OPER")
        {
            PutModule(t_s("Storing oper password in case of reconnect. "
                          "To erase it, use 'forget'."));
            m_bAutoOper = true;
            m_sOperUser = sLine.Token(1);
            m_sOperPassword = sLine.Token(2, true);
            if (m_sOperPassword.find(':') == 0) {
                m_sOperPassword.LeftChomp();
            }
        } else if (sLine.Token(0).AsUpper() == "MODE" && GetNetwork() &&
                   GetNetwork()->GetNick() == sLine.Token(1) &&
                   sLine.Token(2) == "+s")
        {
            PutModule(t_s("Storing server notice mask in case of reconnect. "
                          "To erase it, use 'forget'."));
            m_sSnoMask = sLine.Token(3, true);
            if (m_sSnoMask.find(':') == 0) {
                m_sSnoMask.LeftChomp();
            }
        }
        return CONTINUE;
    }

};

template<> void TModInfo<CSchnoper>(CModInfo& Info) {
    Info.SetWikiPage("schnoper");
    Info.SetHasArgs(false);
    Info.SetArgsHelpText("");
}

NETWORKMODULEDEFS(
    CSchnoper,
    t_s("Stores your oper credentials in RAM after you oper, "
        "and re-opers if your connection is lost"))
