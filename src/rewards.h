// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2024 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDS_H
#define REWARDS_H

#include "main.h"

class CRewards 
{
private:
    static const int DB_OPEN_ATTEMPTS       =     3; // number of attempts
    static const int DB_OPEN_WAITING_TIME   = 10000; // ms
public:
    static bool Init(bool fReindex);
    static void Shutdown();
    static int GetDynamicRewardsEpoch(int nHeight);
    static int GetDynamicRewardsEpochHeight(int nHeight);
    static bool IsDynamicRewardsEpochHeight(int nHeight);
    static bool ConnectBlock(CBlockIndex* pindex, CAmount nSubsidy, CCoinsViewCache& coins);
    static bool DisconnectBlock(CBlockIndex* pindex);
    static CAmount GetBlockValue(int nHeight);
};

#endif 