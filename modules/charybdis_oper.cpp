/*
 * Copyright (C) 2004-2014 ZNC, see the NOTICE file for details.
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

#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Modules.h>

#include <deque>
#include <chrono>

using std::vector;
using std::deque;

using std::chrono::steady_clock;
using std::chrono::time_point;
using std::chrono::milliseconds;
using std::chrono::time_point_cast;
using std::chrono::duration;

class CCharybdisOper : public CModule
{
  public:
    typedef time_point<steady_clock, milliseconds> xtime_t;

  private:
    deque<xtime_t> connects;
    deque<xtime_t> disconnects;
    vector<bool> connect_warning;
    vector<bool> disconnect_warning;
    vector<int> factor_ratio;
    vector<CString> factor_name;

  public:
    MODCONSTRUCTOR(CCharybdisOper)
    {
        AddHelpCommand();
        AddCommand("FluxStatus", "",
                   t_d("Shows stats about rates of client connections "
                       "and disconnections (depends on SNOMASK F/c)."),
                   [=](const CString& sLine) { ShowClientFluxStats(sLine); });
    }

    ~CCharybdisOper() {}

    virtual bool OnLoad(const CString& sArgs, CString& sMessage) {
        connect_warning = vector<bool>(4, false);
        disconnect_warning = vector<bool>(4, false);

        factor_ratio.push_back(32); factor_ratio.push_back(8);
        factor_ratio.push_back(4); factor_ratio.push_back(2);

        factor_name.push_back(t_s("steep spike"));
        factor_name.push_back(t_s("sudden flux"));
        factor_name.push_back(t_s("wide spike"));
        factor_name.push_back(t_s("elevated activity"));
        return true;
    }

    virtual EModRet OnServerNotice(CNick& Nick, CString& sMessage) {
        if (sMessage.find("*** Notice -- Client connecting: ") == 0) {
            connects.push_front(time_point_cast<milliseconds>(
                steady_clock::now()));
            while (connects.size() > 129) {
                connects.pop_back();
            }
            if (CheckClientFlux(true)) {
                ClientFluxStats();
            }
        } else if (sMessage.find("*** Notice -- Client exiting: ") == 0) {
            disconnects.push_front(time_point_cast<milliseconds>(
                steady_clock::now()));
            while (disconnects.size() > 129) {
                disconnects.pop_back();
            }
            if (CheckClientFlux(false)) {
                ClientFluxStats();
            }
        }

        return CONTINUE;
    }

    /**
     *    Checks the connect or disconnect statistics and attempts
     *    to identify unusual local behavior.
     *    PARAM: useConnects (true for connects, false for disconnects)
     *    PARAM: fullSummary (true to report all, false for incremental)
     *    RETURN: true if any events were reported
     */
    bool CheckClientFlux(bool useConnects, bool fullSummary = false)
    {
        bool wasThereAnEvent = false;
        CString type(useConnects ? t_s("connect") : t_s("disconnect"));
        deque<xtime_t> &dataset(useConnects ? connects : disconnects);
        vector<bool> &dataset_warning(
            useConnects ? connect_warning : disconnect_warning);
        if (dataset.size() < 16) {
            if (fullSummary) {
                PutModule(t_s(" * Not enough tracked events to make a "
                              "reasonable conjecture."));
            }
            return false;
        }
        milliseconds avgs[8];
        for (int shift = 0; shift < 8; ++shift) {
            if ((1 << shift) < dataset.size()) {
                avgs[shift] = (dataset[0] - dataset[1<<shift]) / (1<<shift);
            }
            else avgs[shift] = avgs[shift - 1];
        }

        for (int i = 0; i < 4; ++i) {
            if (avgs[i + 2] * factor_ratio[i] < avgs[i + 4]) {
                if (!dataset_warning[i] || fullSummary) {
                    PutModule(t_f("Detected {1}: {2}")(factor_name[i], type));
                    dataset_warning[i] = true;
                    wasThereAnEvent = true;
                }
            } else dataset_warning[i] = false;
        }
        return wasThereAnEvent;
    }

    /**
     *    Prints a summary of collected statistics for review by user.
     *    PARAM: fullSummary (true to report all, false for incremental)
     */

    void ClientFluxStats(bool fullSummary = false) {
        PutModule(t_s("---------- Connect stats ---------"));
        if (connects.size() < 16) {
            PutModule(t_s("*** Not enough data to attempt to analyze spikes!"));
        } else if (fullSummary) {
            CheckClientFlux(true, true);
        }
        for (int i = 2; i < 8; ++i) {
            if ((1 << i) < connects.size()) {
                PutModule(t_f("Average time over {1} connections: {2} ms")(
                    1 << i,
                    ((connects[0] - connects[1 << i]) / (1 << i)).count()));
            }
        }
        PutModule(t_s("-------- Disconnect stats --------"));
        if (disconnects.size() < 16) {
            PutModule(t_s("*** Not enough data to attempt to analyze spikes!"));
        } else if (fullSummary) {
            CheckClientFlux(false, true);
        }
        for (int i = 2; i < 8; ++i) {
            if ((1 << i) < disconnects.size()) {
                PutModule(t_f("Average time over {1} connections: {2} ms")(
                    1 << i,
                    ((disconnects[0] - disconnects[1<<i]) / (1 << i)).count()));
            }
        }
    }

    /**
     *    User command target
     *    PARAM: sLine (ignored)
     */

    void ShowClientFluxStats(const CString& sLine = "") {
        ClientFluxStats(true);
    }
};

template<> void TModInfo<CCharybdisOper>(CModInfo& Info) {
    Info.SetWikiPage("charybdis_oper");
    Info.SetHasArgs(false);
}

USERMODULEDEFS(
    CCharybdisOper,
    t_s("Useful tools for IRC operators on charybdis and forks."))

