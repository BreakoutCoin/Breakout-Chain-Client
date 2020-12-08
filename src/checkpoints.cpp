// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "txdb.h"
#include "main.h"
#include "uint256.h"

extern std::map<uint256, CBlockIndex*> mapBlockIndex;


namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;

    //
    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    //
    static MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (           0, hashGenesisBlock )
        (       13300, uint256("0x62d199c756a0f19a0fc1f9e895190b29cb7b7683d284bfcabacbe906df530ff9"))
        (       37500, uint256("0x0000000000312775281468709d39faf0eb1c099ea35f1a39fe4ba0847658bfe0"))
        (       87000, uint256("0x56fe7fe10f4e8a17d752ce743f5cca1c10e4c6a8eeeabfacd235962148441fb5"))
        (      107000, uint256("0x000000000044e26363bec8f66178fd83a26e5089f6d85e03a9aaf84c50569fcf"))
        (      150000, uint256("0xd2c398443fbc3c490a210e677d924c59dc4d7df01a4297f97b73211ee2de63b3"))
        (      200000, uint256("0x00000000121c43828b0e925fb3c57ac5192641058adc4b91b5e72d836d0f7c7c"))
        (      250000, uint256("0x5590bfced533bb737794cbdbe28f7f928c767fb0352f8d1ceb4da9825ebae7b8"))
        (      300000, uint256("0x754327cdf1439369109273430ee7e801ef197eb0f830420f99dd966e14378a7d"))
        (      400000, uint256("0x32b9922ae1f398e5d812c1b995e12a054f5d0898819d7f0c9752fc2c077e349e"))
        (      500000, uint256("0xf90c2e88d08acb04c49b74ca4c49968f486b3f497a0f886c85bbd8b8a3dba317"))
        (      600000, uint256("0x0365f224a3d71beb68752597ab1a99954f7a783e2d9b6f6a975c2834cc86c6e6"))
        (      700000, uint256("0xd2ea4de23e9b1164741a4d8ca07fcb910821763acc0397375ba884b7ba18b778"))
        (      800000, uint256("0xfca021e1d2000195236ee764f7dd1642527eda417c19745713e1ff4e2ca1a4fc"))
        (      835000, uint256("0xb7a86c11e845f306378ee1f41d8505040e0b7a7d0820e32014a3f9a900cee242"))
    ;

    // TestNet has no checkpoints
    static MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, hashGenesisBlockTestNet )
        ;

    bool CheckHardened(int nHeight, const uint256& hash)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        if (i == checkpoints.end())
        {
            return true;
        }
        return hash == i->second;
    }

    int GetTotalBlocksEstimate()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.rbegin()->first;
    }

    CBlockIndex* GetLastCheckpoint()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            const uint256& hash = i.second;
            std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
            {
                return t->second;
            }
        }
        return pindexGenesisBlock;
    }

    const uint256* GetLastCheckpointHash()
    {
        return GetLastCheckpoint()->phashBlock;
    }

    // initialized at startup as the highest hardened checkpoint
    uint256 hashHardenedCheckpoint = 0;
}
