/*
 * Copyright (C) 2004-2018 ZNC, see the NOTICE file for details.
 * Copyright (C) 2006-2007, CNU <bshalm@broadpark.no>
 *(http://cnu.dieplz.net/znc)
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
#include <znc/IRCNetwork.h>
#include <znc/Chan.h>
#include <znc/Server.h>
#include <time.h>
#include <algorithm>

using std::vector;
using std::map;
using std::exception;
using std::runtime_error;

class CLogRule {
  public:
    CLogRule(const CString& sRule, bool bEnabled = true)
        : m_sRule(sRule), m_bEnabled(bEnabled) {}

    const CString& GetRule() const { return m_sRule; }
    bool IsEnabled() const { return m_bEnabled; }
    void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }

    bool Compare(const CString& sTarget) const {
        return sTarget.WildCmp(m_sRule, CString::CaseInsensitive);
    }

    bool operator==(const CLogRule& sOther) const {
        return m_sRule == sOther.GetRule();
    }

    CString ToString() const { return (m_bEnabled ? "" : "!") + m_sRule; }

  private:
    CString m_sRule;
    bool m_bEnabled;
};

class CLogMod : public CModule {
  public:
    MODCONSTRUCTOR(CLogMod) {
        m_bSanitize = false;
        AddHelpCommand();
        AddCommand(
            "SetRules", t_d("<rules>"),
            t_d("Set logging rules, use !#chan or !query to negate and * "),
            [=](const CString& sLine) { SetRulesCmd(sLine); });
        AddCommand(
            "SetMessageRules", t_d("<rules>"),
            t_d("Set rules to log extra, nonstandard messages from IRC"),
            [=](const CString& sLine) { SetMessageRulesCmd(sLine); });
        AddCommand(
            "ClearMessageRules", "",
            t_d("Clear all nonstandard logging rules"),
            [=](const CString& sLine) { ClearMessageRulesCmd(sLine); });
        AddCommand(
            "SetExtraRules", t_d("<rules>"),
            t_d("Set rules to log from IRC by pattern-match"),
            [=](const CString& sLine) { SetExtraRulesCmd(sLine); });
        AddCommand(
            "ClearExtraRules", "",
            t_d("Clear all pattern-match logging rules"),
            [=](const CString& sLine) { ClearExtraRulesCmd(sLine); });
        AddCommand("ClearRules", "", t_d("Clear all logging rules"),
                   [=](const CString& sLine) { ClearRulesCmd(sLine); });
        AddCommand("ListRules", "", t_d("List all logging rules"),
                   [=](const CString& sLine) { ListRulesCmd(sLine); });
        AddCommand(
            "Set", t_d("<var> true|false"),
            t_d("Set one of the following options: joins, quits, nickchanges, snotices"),
            [=](const CString& sLine) { SetCmd(sLine); });
        AddCommand("ShowSettings", "",
                   t_d("Show current settings set by Set command"),
                   [=](const CString& sLine) { ShowSettingsCmd(sLine); });
    }

    ~CLogMod() override;

    void SetRulesCmd(const CString& sLine);
    void SetMessageRulesCmd(const CString &sLine);
    void SetExtraRulesCmd(const CString &sLine);
    void ClearRulesCmd(const CString& sLine);
    void ClearMessageRulesCmd(const CString &sLine);
    void ClearExtraRulesCmd(const CString &sLine);
    void ListRulesCmd(const CString& sLine = "");
    void SetCmd(const CString& sLine);
    void ShowSettingsCmd(const CString& sLine);

    void SetRules(const VCString& vsRules);
    void SetMessageRules(const CString& sMsgRules);
    void SetExtraRules(const CString& sExtraRules);
    VCString SplitRules(const CString& sRules) const;
    CString JoinRules(const CString& sSeparator) const;
    bool TestRules(const CString& sTarget) const;

    void PutLog(const CString& sLine, const CString& sWindow = "znc.status");
    void PutLog(const CString& sLine, const CChan& Channel);
    void PutLog(const CString& sLine, const CNick& Nick);
    CString GetServer();

    bool OnLoad(const CString& sArgs, CString& sMessage) override;
    void OnIRCConnected() override;
    void OnIRCDisconnected() override;
    EModRet OnBroadcast(CString& sMessage) override;

    void OnRawMode2(const CNick* pOpNick, CChan& Channel, const CString& sModes,
                    const CString& sArgs) override;
    void OnKick(const CNick& OpNick, const CString& sKickedNick, CChan& Channel,
                const CString& sMessage) override;
    void OnQuit(const CNick& Nick, const CString& sMessage,
                const vector<CChan*>& vChans) override;
    void OnJoin(const CNick& Nick, CChan& Channel) override;
    void OnPart(const CNick& Nick, CChan& Channel,
                const CString& sMessage) override;
    void OnNick(const CNick& OldNick, const CString& sNewNick,
                const vector<CChan*>& vChans) override;
    EModRet OnTopic(CNick& Nick, CChan& Channel, CString& sTopic) override;
    EModRet OnPrivCTCP(CNick& Nick, CString& sMessage) override;
    EModRet OnChanCTCP(CNick& Nick, CChan& Channel, CString& sMessage) override;
    EModRet OnCTCPReply(CNick& Nick, CString& sMessage);

    EModRet OnSendToIRCMessage(CMessage& Message) override;

    /* notices */
    EModRet OnUserNotice(CString& sTarget, CString& sMessage) override;
    EModRet OnServerNoticeMessage(CNoticeMessage& Message) override;
    EModRet OnPrivNotice(CNick& Nick, CString& sMessage) override;
    EModRet OnChanNotice(CNick& Nick, CChan& Channel,
                         CString& sMessage) override;

    /* actions */
    EModRet OnUserAction(CString& sTarget, CString& sMessage) override;
    EModRet OnPrivAction(CNick& Nick, CString& sMessage) override;
    EModRet OnChanAction(CNick& Nick, CChan& Channel,
                         CString& sMessage) override;

    /* msgs */
    EModRet OnUserMsg(CString& sTarget, CString& sMessage) override;
    EModRet OnPrivMsg(CNick& Nick, CString& sMessage) override;
    EModRet OnChanMsg(CNick& Nick, CChan& Channel, CString& sMessage) override;

    EModRet OnRaw(CString &sLine) override;

    /* logfile cache */
    CFile& CacheLookup(const CString& filename);
    void CacheNudge(const CString& filename);
    void CacheProcess(const CString& filename);
    void CacheKill(const CString& filename);

    void CacheNudgeAll();
    void CacheKillAll();
    void CacheProcessOne();
    void CacheProcessAll();

    bool MatchesExtraLogging(const CString &sLine);
    void LogDefrag(const CString &filename);

  private:
    bool NeedJoins() const;
    bool NeedQuits() const;
    bool NeedNickChanges() const;
    bool NeedServerNotices() const;

    CString m_sLogPath;
    CString m_sTimestamp;
    bool m_bSanitize;
    vector<CLogRule> m_vRules;
    SCString m_ssMsgRules;
    VCString m_vsExtraRules;

    CString              m_sLastDay;
    map<CString, CFile*> m_LogCache;
    map<CString, CFile*> m_ExpCache;
};

void CLogMod::SetRulesCmd(const CString& sLine) {
    VCString vsRules = SplitRules(sLine.Token(1, true));

    if (vsRules.empty()) {
        PutModule(t_s("Usage: SetRules <rules>"));
        PutModule(t_s("Wildcards are allowed"));
    } else {
        SetRules(vsRules);
        SetNV("rules", JoinRules(","));
        ListRulesCmd();
    }
}

void CLogMod::SetMessageRulesCmd(const CString& sLine) {
    CString sMsgRules = sLine.Token(1, true);

    if (sMsgRules.empty()) {
        PutModule(t_s("Usage: SetMessageRules <rules>"));
    } else {
        SetMessageRules(sMsgRules);
        SetNV("messagerules", sMsgRules);
        ListRulesCmd();
    }
}

void CLogMod::SetExtraRulesCmd(const CString& sLine) {
    CString sExtraRules = sLine.Token(1, true);

    if (sExtraRules.empty()) {
        PutModule(t_s("Usage: SetExtraRules <rules>"));
    } else {
        SetExtraRules(sExtraRules);
        SetNV("extrarules", sExtraRules);
        ListRulesCmd();
    }
}

void CLogMod::ClearRulesCmd(const CString& sLine) {
    size_t uCount = m_vRules.size();

    if (uCount == 0) {
        PutModule(t_s("No logging rules. Everything is logged."));
    } else {
        CString sRules = JoinRules(" ");
        SetRules(VCString());
        DelNV("rules");
        PutModule(t_p("1 rule removed: {2}", "{1} rules removed: {2}", uCount)(
            uCount, sRules));
    }
}

void CLogMod::ClearMessageRulesCmd(const CString& sLine) {
    size_t uCount = m_ssMsgRules.size();

    if (uCount == 0) {
        PutModule(t_s("No message rules."));
    } else {
        CString sep("\" \"");
        CString sRules =
            "\"" + sep.Join(m_ssMsgRules.begin(), m_ssMsgRules.end()) + "\"";
        SetMessageRules("");
        DelNV("messagerules");
        PutModule(t_p("1 rule removed: {2}", "{1} rules removed: {2}", uCount)(
            uCount, sRules));
    }
}

void CLogMod::ClearExtraRulesCmd(const CString& sLine) {
    size_t uCount = m_vsExtraRules.size();

    if (uCount == 0) {
        PutModule(t_s("No extra rules."));
    } else {
        CString sep("\" \"");
        CString sRules =
            "\"" + sep.Join(m_vsExtraRules.begin(),m_vsExtraRules.end()) + "\"";
        SetExtraRules("");
        DelNV("extrarules");
        PutModule(t_p("1 rule removed: {2}", "{1} rules removed: {2}", uCount)(
            uCount, sRules));
    }
}

void CLogMod::ListRulesCmd(const CString& sLine) {
    CTable Table;
    Table.AddColumn(t_s("Rule", "listrules"));
    Table.AddColumn(t_s("Logging enabled", "listrules"));

    for (const CLogRule& Rule : m_vRules) {
        Table.AddRow();
        Table.SetCell(t_s("Rule", "listrules"), Rule.GetRule());
        Table.SetCell(t_s("Logging enabled", "listrules"),
                      CString(Rule.IsEnabled()));
    }

    if (Table.empty()) {
        PutModule(t_s("No logging rules. Everything is logged."));
    } else {
        PutModule(Table);
    }

    CTable MsgTable;
    MsgTable.AddColumn(t_s("Rule", "listrules"));

    for (const CString& Rule : m_ssMsgRules) {
        MsgTable.AddRow();
        MsgTable.SetCell(t_s("Rule", "listrules"), Rule);
    }

    if (MsgTable.empty()) {
        PutModule(t_s("No message logging rules."));
    } else {
        PutModule(MsgTable);
    }

    CTable ExtraTable;
    ExtraTable.AddColumn(t_s("Rule", "listrules"));

    for (const CString& Rule : m_vsExtraRules) {
        ExtraTable.AddRow();
        ExtraTable.SetCell(t_s("Rule", "listrules"), Rule);
    }

    if (ExtraTable.empty()) {
        PutModule(t_s("No extra wildcard logging rules."));
    } else {
        PutModule(ExtraTable);
    }
}

void CLogMod::SetCmd(const CString& sLine) {
    const CString sVar = sLine.Token(1).AsLower();
    const CString sValue = sLine.Token(2);
    if (sValue.empty()) {
        PutModule(
            t_s("Usage: Set <var> true|false, where <var> is one of: joins, "
                "quits, nickchanges, snotices"));
        return;
    }
    bool b = sLine.Token(2).ToBool();
    const std::unordered_map<CString, std::pair<CString, CString>>
        mssResponses = {
            {"joins", {t_s("Will log joins"), t_s("Will not log joins")}},
            {"quits", {t_s("Will log quits"), t_s("Will not log quits")}},
            {"nickchanges",
             {t_s("Will log nick changes"), t_s("Will not log nick changes")}},
            {"snotices", {t_s("Will log server notices"),
                          t_s("Will not log server notices")}}};
    auto it = mssResponses.find(sVar);
    if (it == mssResponses.end()) {
        PutModule(t_s(
            "Unknown variable. Known variables: "
            "joins, quits, nickchanges, snotices"));
        return;
    }
    SetNV(sVar, CString(b));
    PutModule(b ? it->second.first : it->second.second);
}

void CLogMod::ShowSettingsCmd(const CString& sLine) {
    PutModule(NeedJoins() ? t_s("Logging joins") : t_s("Not logging joins"));
    PutModule(NeedQuits() ? t_s("Logging quits") : t_s("Not logging quits"));
    PutModule(NeedNickChanges() ? t_s("Logging nick changes")
                                : t_s("Not logging nick changes"));
    PutModule(NeedServerNotices() ? t_s("Logging server notices")
                                : t_s("Not logging server notices"));
}

bool CLogMod::NeedJoins() const {
    return !HasNV("joins") || GetNV("joins").ToBool();
}

bool CLogMod::NeedQuits() const {
    return !HasNV("quits") || GetNV("quits").ToBool();
}

bool CLogMod::NeedNickChanges() const {
    return !HasNV("nickchanges") || GetNV("nickchanges").ToBool();
}

bool CLogMod::NeedServerNotices() const {
    return !HasNV("snotices") || GetNV("snotices").ToBool();
}

void CLogMod::SetRules(const VCString& vsRules) {
    m_vRules.clear();

    for (CString sRule : vsRules) {
        bool bEnabled = !sRule.TrimPrefix("!");
        m_vRules.push_back(CLogRule(sRule, bEnabled));
    }
}

void CLogMod::SetMessageRules(const CString& sRules) {
    m_ssMsgRules.clear();
    VCString vsMsgRules;
    sRules.QuoteSplit(vsMsgRules);
    m_ssMsgRules.insert(vsMsgRules.begin(), vsMsgRules.end());
}

void CLogMod::SetExtraRules(const CString& sRules) {
    m_vsExtraRules.clear();
    sRules.QuoteSplit(m_vsExtraRules);
}

VCString CLogMod::SplitRules(const CString& sRules) const {
    CString sCopy = sRules;
    sCopy.Replace(",", " ");

    VCString vsRules;
    sCopy.Split(" ", vsRules, false, "", "", true, true);

    return vsRules;
}

CString CLogMod::JoinRules(const CString& sSeparator) const {
    VCString vsRules;
    for (const CLogRule& Rule : m_vRules) {
        vsRules.push_back(Rule.ToString());
    }

    return sSeparator.Join(vsRules.begin(), vsRules.end());
}

bool CLogMod::TestRules(const CString& sTarget) const {
    for (const CLogRule& Rule : m_vRules) {
        if (Rule.Compare(sTarget)) {
            return Rule.IsEnabled();
        }
    }

    return true;
}

void CLogMod::PutLog(const CString& sLine,
                     const CString& sWindow /*= "Status"*/) {
    if (!TestRules(sWindow)) {
        return;
    }

    CString sPath;
    timeval curtime;

    gettimeofday(&curtime, nullptr);
    // Generate file name
    sPath = CUtils::FormatTime(curtime, m_sLogPath, GetUser()->GetTimezone());
    if (sPath.empty()) {
        DEBUG("Could not format log path [" << sPath << "]");
        return;
    }

    if (sPath != m_sLastDay) {
        m_sLastDay = sPath;
        CacheKillAll();
    }

    // TODO: Properly handle IRC case mapping
    // $WINDOW has to be handled last, since it can contain %
    sPath.Replace("$USER", GetUser() ? GetUser()->GetUserName() : "UNKNOWN");
    sPath.Replace("$NETWORK", GetNetwork() ? GetNetwork()->GetName() : "znc");
    sPath.Replace("$WINDOW", sWindow.Replace_n("/", "-")
                                    .Replace_n("\\", "-").AsLower());

    // Check if it's allowed to write in this specific path
    if (sPath.empty()) {
        DEBUG("Invalid log path [" << m_sLogPath << "].");
        return;
    }

    CacheProcessOne();
    try {
        CFile &LogFile = CacheLookup(sPath);
        if (!LogFile.Open(O_WRONLY | O_APPEND | O_CREAT)) {
            throw runtime_error("Could not open log file: " + sPath);
        }
        LogFile.Write(CUtils::FormatTime(curtime, m_sTimestamp,
                                         GetUser()->GetTimezone()) +
                      " " + (m_bSanitize ? sLine.StripControls_n() : sLine) +
                      "\n");
        LogFile.Close();
    } catch (exception &e) {
        DEBUG("Could not open log file [" << sPath << "]: " << e.what());
    }
}

void CLogMod::PutLog(const CString& sLine, const CChan& Channel) {
    PutLog(sLine, Channel.GetName());
}

void CLogMod::PutLog(const CString& sLine, const CNick& Nick) {
    PutLog(sLine, Nick.GetNick());
}

CString CLogMod::GetServer() {
    CServer* pServer = GetNetwork()->GetCurrentServer();
    CString sSSL;

    if (!pServer) return "(no server)";

    if (pServer->IsSSL()) sSSL = "+";
    return pServer->GetName() + " " + sSSL + CString(pServer->GetPort());
}

bool CLogMod::OnLoad(const CString& sArgs, CString& sMessage) {
    VCString vsArgs;
    sArgs.QuoteSplit(vsArgs);

    bool bReadingTimestamp = false;
    bool bHaveLogPath = false;

    for (CString& sArg : vsArgs) {
        if (bReadingTimestamp) {
            m_sTimestamp = sArg;
            bReadingTimestamp = false;
        } else if (sArg.Equals("-sanitize")) {
            m_bSanitize = true;
        } else if (sArg.Equals("-timestamp")) {
            bReadingTimestamp = true;
        } else {
            // Only one arg may be LogPath
            if (bHaveLogPath) {
                sMessage =
                    t_f("Invalid args [{1}]. Only one log path allowed.  Check "
                        "that there are no spaces in the path.")(sArgs);
                return false;
            }
            m_sLogPath = sArg;
            bHaveLogPath = true;
        }
    }

    if (m_sTimestamp.empty()) {
        m_sTimestamp = "[%H:%M:%S]";
    }

    // Add default filename to path if it's a folder
    if (GetType() == CModInfo::UserModule) {
        if (m_sLogPath.Right(1) == "/" ||
            m_sLogPath.find("$WINDOW") == CString::npos ||
            m_sLogPath.find("$NETWORK") == CString::npos) {
            if (!m_sLogPath.empty()) {
                m_sLogPath += "/";
            }
            m_sLogPath += "$NETWORK/$WINDOW/%Y-%m-%d.log";
        }
    } else if (GetType() == CModInfo::NetworkModule) {
        if (m_sLogPath.Right(1) == "/" ||
            m_sLogPath.find("$WINDOW") == CString::npos) {
            if (!m_sLogPath.empty()) {
                m_sLogPath += "/";
            }
            m_sLogPath += "$WINDOW/%Y-%m-%d.log";
        }
    } else {
        if (m_sLogPath.Right(1) == "/" ||
            m_sLogPath.find("$USER") == CString::npos ||
            m_sLogPath.find("$WINDOW") == CString::npos ||
            m_sLogPath.find("$NETWORK") == CString::npos) {
            if (!m_sLogPath.empty()) {
                m_sLogPath += "/";
            }
            m_sLogPath += "$USER/$NETWORK/$WINDOW/%Y-%m-%d.log";
        }
    }

    CString sRules = GetNV("rules");
    VCString vsRules = SplitRules(sRules);
    SetRules(vsRules);

    SetMessageRules(GetNV("messagerules"));
    SetExtraRules(GetNV("extrarules"));

    // Check if it's allowed to write in this path in general
    m_sLogPath = CDir::CheckPathPrefix(GetSavePath(), m_sLogPath);
    if (m_sLogPath.empty()) {
        sMessage = t_f("Invalid log path [{1}]")(m_sLogPath);
        return false;
    } else {
        sMessage = t_f("Logging to [{1}]. Using timestamp format '{2}'")(
            m_sLogPath, m_sTimestamp);
        return true;
    }
}

// TODO consider writing translated strings to log. Currently user language
// affects only UI.
void CLogMod::OnIRCConnected() {
    PutLog("Connected to IRC (" + GetServer() + ")");
}

void CLogMod::OnIRCDisconnected() {
    PutLog("Disconnected from IRC (" + GetServer() + ")");
}

CModule::EModRet CLogMod::OnBroadcast(CString& sMessage) {
    PutLog("Broadcast: " + sMessage);
    return CONTINUE;
}

void CLogMod::OnRawMode2(const CNick* pOpNick, CChan& Channel,
                         const CString& sModes, const CString& sArgs) {
    const CString sNick = pOpNick ? pOpNick->GetNick() : "Server";
    PutLog("*** " + sNick + " sets mode: " + sModes + " " + sArgs, Channel);
}

void CLogMod::OnKick(const CNick& OpNick, const CString& sKickedNick,
                     CChan& Channel, const CString& sMessage) {
    PutLog("*** " + sKickedNick + " was kicked by " + OpNick.GetNick() + " (" +
               sMessage + ")",
           Channel);
}

void CLogMod::OnQuit(const CNick& Nick, const CString& sMessage,
                     const vector<CChan*>& vChans) {
    if (NeedQuits()) {
        for (CChan* pChan : vChans)
            PutLog("*** Quits: " + Nick.GetNick() + " (" + Nick.GetIdent() +
                       "@" + Nick.GetHost() + ") (" + sMessage + ")",
                   *pChan);
    }
}

CModule::EModRet CLogMod::OnSendToIRCMessage(CMessage& Message) {
    if (Message.GetType() != CMessage::Type::Quit) {
        return CONTINUE;
    }
    CIRCNetwork* pNetwork = Message.GetNetwork();
    OnQuit(pNetwork->GetIRCNick(),
            Message.As<CQuitMessage>().GetReason(),
            pNetwork->GetChans());
    return CONTINUE;
}

void CLogMod::OnJoin(const CNick& Nick, CChan& Channel) {
    if (NeedJoins()) {
        PutLog("*** Joins: " + Nick.GetNick() + " (" + Nick.GetIdent() + "@" +
                   Nick.GetHost() + ")",
               Channel);
    }
}

void CLogMod::OnPart(const CNick& Nick, CChan& Channel,
                     const CString& sMessage) {
    PutLog("*** Parts: " + Nick.GetNick() + " (" + Nick.GetIdent() + "@" +
               Nick.GetHost() + ") (" + sMessage + ")",
           Channel);
}

void CLogMod::OnNick(const CNick& OldNick, const CString& sNewNick,
                     const vector<CChan*>& vChans) {
    if (NeedNickChanges()) {
        for (CChan* pChan : vChans)
            PutLog("*** " + OldNick.GetNick() + " is now known as " + sNewNick,
                   *pChan);
    }
}

CModule::EModRet CLogMod::OnTopic(CNick& Nick, CChan& Channel,
                                  CString& sTopic) {
    PutLog("*** " + Nick.GetNick() + " changes topic to '" + sTopic + "'",
           Channel);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnCTCPReply(CNick& Nick, CString& sMessage)
{
    PutLog("*** " + Nick.GetNick() + " CTCP-REPLY "
                  + sMessage.Trim_n("\x001"), Nick);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnPrivCTCP(CNick& Nick, CString& sMessage)
{
    // skip this case since we already log it with OnPrivAction
    if (sMessage.substr(0, 7) != "ACTION ")
        PutLog("*** " + Nick.GetNick() + " CTCP "
                      + sMessage.Trim_n("\x001"), Nick);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnChanCTCP(CNick& Nick, CChan& Channel, CString& sMsg)
{
    // skip this case since we already log it with OnChanAction
    if (sMsg.substr(0, 7) != "ACTION ")
        PutLog("*** " + Nick.GetNick() + " CTCP "
                      + sMsg.Trim_n("\x001"), Channel);
    return CONTINUE;
}

/* notices */
CModule::EModRet CLogMod::OnUserNotice(CString& sTarget, CString& sMessage) {
    CIRCNetwork* pNetwork = GetNetwork();
    if (pNetwork) {
        PutLog("-" + pNetwork->GetCurNick() + "- " + sMessage, sTarget);
    }

    return CONTINUE;
}

CModule::EModRet CLogMod::OnServerNoticeMessage(CNoticeMessage &Message) {
    if (!NeedServerNotices()) {
        return CONTINUE;
    }
    CNick nick = Message.GetNick();
    CString sText = Message.GetText();
    PutLog("-" + nick.GetNick() + "- " + sText, "server.notices");
    return CONTINUE;
}

CModule::EModRet CLogMod::OnPrivNotice(CNick& Nick, CString& sMessage) {
    PutLog("-" + Nick.GetNick() + "- " + sMessage, Nick);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnChanNotice(CNick& Nick, CChan& Channel,
                                       CString& sMessage) {
    PutLog("-" + Nick.GetNick() + "- " + sMessage, Channel);
    return CONTINUE;
}

/* actions */
CModule::EModRet CLogMod::OnUserAction(CString& sTarget, CString& sMessage) {
    CIRCNetwork* pNetwork = GetNetwork();
    if (pNetwork) {
        PutLog("* " + pNetwork->GetCurNick() + " " + sMessage, sTarget);
    }

    return CONTINUE;
}

CModule::EModRet CLogMod::OnPrivAction(CNick& Nick, CString& sMessage) {
    PutLog("* " + Nick.GetNick() + " " + sMessage, Nick);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnChanAction(CNick& Nick, CChan& Channel,
                                       CString& sMessage) {
    PutLog("* " + Nick.GetNick() + " " + sMessage, Channel);
    return CONTINUE;
}

/* msgs */
CModule::EModRet CLogMod::OnUserMsg(CString& sTarget, CString& sMessage) {
    CIRCNetwork* pNetwork = GetNetwork();
    if (pNetwork) {
        PutLog("<" + pNetwork->GetCurNick() + "> " + sMessage, sTarget);
    }

    return CONTINUE;
}

CModule::EModRet CLogMod::OnPrivMsg(CNick& Nick, CString& sMessage) {
    PutLog("<" + Nick.GetNick() + "> " + sMessage, Nick);
    return CONTINUE;
}

CModule::EModRet CLogMod::OnChanMsg(CNick& Nick, CChan& Channel,
                                    CString& sMessage) {
    PutLog("<" + Nick.GetNick() + "> " + sMessage, Channel);
    return CONTINUE;
}

CFile &CLogMod::CacheLookup(const CString &filename) {
    CFile new_file(filename);
    CString full_name = new_file.GetLongName();

    CString sLogDir = new_file.GetDir();
    struct stat ModDirInfo;
    CFile::GetInfo(GetSavePath(), ModDirInfo);
    if (!CFile::Exists(sLogDir)) CDir::MakeDir(sLogDir, ModDirInfo.st_mode);

    if (m_LogCache.find(full_name) != m_LogCache.end()) {}
    else if (m_ExpCache.find(full_name) != m_ExpCache.end()) {
        m_LogCache[full_name] = m_ExpCache[full_name];
    } else {
        CFile *cf = NULL;
        try {
            cf = new CFile(new_file);
            m_LogCache[full_name] = cf;
        } catch (exception &e) {
            m_LogCache.erase(full_name);
            if (cf) delete cf;
            throw;
        }
    }
    return *m_LogCache[full_name];
}

void CLogMod::CacheNudge(const CString &filename) {
    CFile new_file(filename);
    CString full_name = new_file.GetLongName();
    if (m_LogCache.find(full_name) != m_LogCache.end()) {
        m_ExpCache[full_name] = m_LogCache[full_name];
    }
    return;
}

void CLogMod::CacheKill(const CString &filename) {
    CFile new_file(filename);
    CString full_name = new_file.GetLongName();
    if (m_LogCache.find(full_name) != m_LogCache.end()) {
        m_ExpCache[full_name] = m_LogCache[full_name];
        m_LogCache.erase(full_name);
    }
    return;
}

void CLogMod::CacheProcess(const CString &filename)
{
    CFile new_file(filename);
    CString full_name = new_file.GetLongName();
    if (m_ExpCache.find(full_name) != m_ExpCache.end()) {
        LogDefrag(full_name);
        if (m_LogCache.find(full_name) == m_LogCache.end()) {
            delete m_ExpCache[full_name];
        }
        m_ExpCache.erase(full_name);
    }
    return;
}

void CLogMod::CacheNudgeAll()
{
    m_ExpCache.insert(m_LogCache.begin(), m_LogCache.end());
}

void CLogMod::CacheKillAll()
{
    CacheNudgeAll();
    m_LogCache.clear();
}

void CLogMod::CacheProcessOne()
{
    if (!m_ExpCache.empty()) CacheProcess(m_ExpCache.begin()->first);
}

void CLogMod::CacheProcessAll()
{
    while (!m_ExpCache.empty()) CacheProcess(m_ExpCache.begin()->first);
}

void CLogMod::LogDefrag(const CString &filename)
{
    CString newfilename(filename + ".new");
    if (!CFile::Copy(filename, newfilename, true)) {
        PutModule("Log defragment failed: copy! (" + filename + ")");
    } else if (!CFile::Move(newfilename, filename, true)) {
        PutModule("Log defragment failed: move! (" + filename + ")");
    }
}

bool CLogMod::MatchesExtraLogging(const CString &sLine)
{
    CString messageType(
        (sLine.find(':') == 0 ? sLine.Token(1) : sLine.Token(0)).MakeUpper());
    if (m_ssMsgRules.find(messageType) != m_ssMsgRules.end()) {
        return true;
    }
    for (VCString::iterator i = m_vsExtraRules.begin();
         i != m_vsExtraRules.end();
         ++i) {
        if (sLine.WildCmp(*i)) {
            return true;
        }
    }
    return false;
}

CModule::EModRet CLogMod::OnRaw(CString &sLine)
{
    if (MatchesExtraLogging(sLine)) {
        PutLog(sLine, "logging.extra");
    }
    return CONTINUE;
}

CLogMod::~CLogMod() {
    CacheKillAll();
    CacheProcessAll();
}

template <>
void TModInfo<CLogMod>(CModInfo& Info) {
    Info.AddType(CModInfo::NetworkModule);
    Info.AddType(CModInfo::GlobalModule);
    Info.SetHasArgs(true);
    Info.SetArgsHelpText(
        Info.t_s("[-sanitize] Optional path where to store logs."));
    Info.SetWikiPage("log");
}

USERMODULEDEFS(CLogMod, t_s("Writes IRC logs."))
