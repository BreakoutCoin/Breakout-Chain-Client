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
        (      100000, uint256("0x2a2c53df2ed62faa88065e92c4f6c5f7c99073b4be869fd73358d26dbcd0ff59"))
        (      110000, uint256("0x00000000000e6726735f039ff445d9c4232a986adad5cc66af1accd8b9bb1e4a"))
        (      120000, uint256("0x0d3d9be7cb5231a724a684e8c8a0f977694784bfd0d59d0eec3abaaa7453498e"))
        (      130000, uint256("0xbe417b5d3a28ffee0fbdf9e0f92b2d972e3acc4217988e0d293bfb6ff90f6096"))
        (      140000, uint256("0x44093a05be622712f9ed19087a3bf1e1d05261d72a4756ea1fa207db02d85828"))
        (      150000, uint256("0xd2c398443fbc3c490a210e677d924c59dc4d7df01a4297f97b73211ee2de63b3"))
        (      160000, uint256("0x8cd3d2e07ef631047974631e6c8880f717d96a8e1d751fb07bc426dd160ed734"))
        (      170000, uint256("0x000000000bda1c5fad8ee8d29c1bcb723ccde4f9f463c1bb4ee237bd3cd69de2"))
        (      180000, uint256("0x2eec764ff3b18dacf66ebfbabbffed392f152e931a7f13354433b00979bc95ce"))
        (      190000, uint256("0x0000000007e4ff0befb4333225fc22c845a1cc6c88f2b426d52bac7c51ec444b"))
        (      200000, uint256("0x00000000121c43828b0e925fb3c57ac5192641058adc4b91b5e72d836d0f7c7c"))
        (      210000, uint256("0x00000000017253b183942f157ae806f60027e82bcd2a9b73cf9db1048a142696"))
        (      220000, uint256("0x000000000688f77627693bdc2bfb3135e13397a12be0c6f8d9375f4cefa36dad"))
        (      230000, uint256("0x0000000003ae425be6cbaf650381f831915f669fdea3db65243ee084f2a68b61"))
        (      240000, uint256("0x1e0af43f3a12f136f7005fd3527e618dc1f7865f400899565a20e540eca1b106"))
        (      250000, uint256("0x5590bfced533bb737794cbdbe28f7f928c767fb0352f8d1ceb4da9825ebae7b8"))
        (      260000, uint256("0x600f99b7064e4d0383d9a9532eb5d3efdd8308b54fab2d273071f04e61b8652f"))
        (      270000, uint256("0xa03bde1449a48347894e6ccd1242c4cc7756ecd046405f4125e708f6abebf48d"))
        (      280000, uint256("0xa44e3b9a70b4d7099ce0996d04f079ca4e24a621d9e3ff0cb6d5903e091f551b"))
        (      290000, uint256("0x71cdf579a7090099f8103f764452155956db55e1f07cedaa4d550ca7bb1c5db1"))
        (      300000, uint256("0x754327cdf1439369109273430ee7e801ef197eb0f830420f99dd966e14378a7d"))
        (      310000, uint256("0x0631848bf6289cab973648706b4aa1004bdb2d214c58df6fdc24456e23d86db2"))
        (      320000, uint256("0xef454c5190244c839b4bd44efd33d5b7646686503e8f65ee684d2128d89feedb"))
        (      330000, uint256("0xb6f8bda0ea41fa560121eabc821af4d866f9237b45c08ec501ef58fc5c2b9300"))
        (      340000, uint256("0xf3e47a38d8ace6bb8f31c6b4e0949c6f20703e21c079eee41367649090a9dadc"))
        (      350000, uint256("0x8562281b7c246215793cebb99efa8fdaf22d7aea561486c9c8750d7e8435da34"))
        (      360000, uint256("0x2519972f86b7cbe65d500c35d16232786f323ed1da734604e8fcf8b9fe9e1e79"))
        (      370000, uint256("0xb479d427db1418feac84b8563f32b487459666e5dd0d513ee24a6b7844531b97"))
        (      380000, uint256("0x403d1a10dab253fd7132d6539a50944513b4a17b40eb7f705e0e2f8f12191e0c"))
        (      390000, uint256("0xb11e70bc3a01031ccc6eda366e4fdd5e519619f7c03ca5c5832f00dacceaa4f9"))
        (      400000, uint256("0x32b9922ae1f398e5d812c1b995e12a054f5d0898819d7f0c9752fc2c077e349e"))
        (      410000, uint256("0x33d63636900f040004ca31787e12d0b0899bb2d3a0e3cb292a795dd22a1bd75f"))
        (      420000, uint256("0x06d73fbcd89158ea9a3dd658b99a22ecf4a52f2782f2d0cbe8df7927881a56dc"))
        (      430000, uint256("0xe7521a9c9a887857b1973583f47347fe5717823e987e047708089eb533d7862e"))
        (      440000, uint256("0x4ed1941e2a163c210177cf480f3db7a104ccac499ccf62e2363880c197f6fe69"))
        (      450000, uint256("0x4f6025830ebaf91785ad2792f5efb3a583fbe86201702c4a604c6d707bc48497"))
        (      460000, uint256("0x504b2f73c4482ac5a7ec3dc2c943961d07c76306ae7f84c38def30c7f96557ab"))
        (      470000, uint256("0x352442d1ba0b349d202eb40e768655aa02d96762f0399c4a8cc252d9724d22be"))
        (      480000, uint256("0x2e9e97bacb79073d8b62e124a08e82340b22b2349724c623584830383e164202"))
        (      490000, uint256("0xfed35480bd08ca40a49440de80d43b0ded1b3e29b3f0a291eea4f580acc524c8"))
        (      500000, uint256("0xf90c2e88d08acb04c49b74ca4c49968f486b3f497a0f886c85bbd8b8a3dba317"))
        (      510000, uint256("0x28ecb68f343dfdc0f5192cae070f63369e6f266dd3299a6ad76ba014b03f9081"))
        (      520000, uint256("0x0d165622df3a2e717bff4e4406227b9e4e659f5bf3dd6fd8c0cec7d0ab50a2f3"))
        (      530000, uint256("0x8b26d9581b313e4d69401fda33084c40ad8fb5479bab877c97f890211b07b76a"))
        (      540000, uint256("0x036ddaf420e70999856bbfc945794506d4e5cd9522c1d0aba651c6e6adf33cc7"))
        (      550000, uint256("0x8806212613c1e41ca2b5655a861dd08de456b7db1b4cbaf464def90f3218f043"))
        (      560000, uint256("0xebcdffdbf58498be3fae5592dd929c0ee5b2862ba2bd0f0fe604f28249bfb391"))
        (      570000, uint256("0xef3e91cbbeb3dc03c1a17804a155f172f4963fc0504c5b22e9cac1af2ab3ed32"))
        (      580000, uint256("0xd757b4e678a87201559ac678b4236caf2dfbe005c7d4b6dd8c1a250ee369be65"))
        (      590000, uint256("0x39b506a33e888c40ffb2b8720dc4765af238782673dcb92c66d937986185c280"))
        (      600000, uint256("0x0365f224a3d71beb68752597ab1a99954f7a783e2d9b6f6a975c2834cc86c6e6"))
        (      610000, uint256("0x2a9643246d0ca932f851fc373042092e008c01c4af6d31f20a6d9c9178e0dead"))
        (      620000, uint256("0x57cdc440d5e9c11a82b2c305eb52378f4d760345ab459937d03e705c4ca836ba"))
        (      630000, uint256("0x46ea4a700aa47e81bc8e50e578ad0c12cbae72d56ca99708fffd36f3949cc732"))
        (      640000, uint256("0x709503b998653197edf39979ae5f0f182ca92e4d9d22755c97b1fecc7aeb054e"))
        (      650000, uint256("0x09b720ce7663b04b1206a0cc41d06779a222f32963e440c364a344e861d55005"))
        (      660000, uint256("0xce70964a01678bb04ed2d606e2dd4afc91f4215f7a6e9cc7fddbe3efb40866fa"))
        (      670000, uint256("0xba3f312ac38f68916e1fd26b47d1e65baf8ac07d74a724e85c14f6d18b6c9a1e"))
        (      680000, uint256("0xe6fcf405167b6d03cee9ebb3661d30abd8d2deda4d293c92751672117cc90d26"))
        (      690000, uint256("0x45bad85c102382b04d0db95bc4bbe8be35547da6e2972f100fff4715a1b14560"))
        (      700000, uint256("0xd2ea4de23e9b1164741a4d8ca07fcb910821763acc0397375ba884b7ba18b778"))
        (      710000, uint256("0x0e89869a13e92cd4a6d423a022693de71ad3853305dd75006e72885f5543e0f6"))
        (      720000, uint256("0x0ab2acc837cf6b85823e1682654b6c473b8e4e085b3699b30c4432d53ce4fc04"))
        (      730000, uint256("0x43408d06a4cb552d89209e7a8a7de32fbc8cfde2a6a77564a94d16420cdee3ee"))
        (      740000, uint256("0xb7533d5782d63b96e0e970fbc83006f9f0c8bc37e8ebd3d3ca2152227ce3244b"))
        (      750000, uint256("0x81656691718039a7b6b3317a9bfb50137ea1299f4789c75c0e563e01fd4aa618"))
        (      760000, uint256("0xb87a1e68c4210df1c2da4925e1862652dba05a4d2e77e8db3a221b3f7f3523c3"))
        (      770000, uint256("0x0f07aab4a9181bb145bee698be7feef84f58ecb3a8e18e27f1ec811844072452"))
        (      780000, uint256("0xd097ff27511e22ee6fcac81e615ef370a27a57fc71817c198fc9cb1e1fb592e4"))
        (      790000, uint256("0x273fd5d5154b688a87e6fd975a8439ed245627ae8bdde47bc0a2ab3cb63892dd"))
        (      800000, uint256("0xfca021e1d2000195236ee764f7dd1642527eda417c19745713e1ff4e2ca1a4fc"))
        (      810000, uint256("0x33570f7c9bf59ef5b3dbb50618c6158f817d8234a4d3afe9aefaeb6d4722bd99"))
        (      820000, uint256("0x92de385f7dd3f0ea692b6b524d2883ed883801381bca6a459b8384decf5a9021"))
        (      830000, uint256("0x17158722e88fad8ae86c0ee529fa3c9b3d16d5cd18dd7d62aab98ec3c1547093"))
        (      840000, uint256("0xad8d50d1e087e70a00a4a0138695d241ab2201356a25393f0e0c3f741097948e"))
        (      850000, uint256("0xa54e2a4c2cb95bd3db2bb52c8d7a8bcfe40910d8904d6099ae6240a7c590d2d8"))
        (      860000, uint256("0x92a12a2cc939fdb64baf09de3662c3b94e746bebfffb0be538472ab6ef5d4f46"))
        (      870000, uint256("0xe8847679bb11a90cb44278dfbaf2264f648223869b8b47e64ab1f0df6845f943"))
        (      880000, uint256("0x36d4041364d9d8f45a11172c37dd03c0f2b9abc175de588853a40f0a419c98f1"))
        (      890000, uint256("0x972ff31438242a8aa2d8cb147898b3f12ba1f9b31eb3c5d1346262a2d9944566"))
        (      900000, uint256("0x8d389d9ad6d7493d59e0f7e0bb8ed7b5a83a4263013ea1c590dadfb62649430f"))
        (      910000, uint256("0xe895d00b06f77090c2efb52369ad482b30c88367726c6933aff374c73b564795"))
        (      920000, uint256("0x9310f80812d9e894ff61740701c6b18e38aa1d6cdd2cec82df0e221341507e26"))
        (      930000, uint256("0x2b2a578a3e8d72269338f3c936474ea8383d4035167383bb525606b88e14275d"))
        (      940000, uint256("0xc440fb38dfb7b84ef05f747f9cceb27c14d9cdd72ec6cf613204ef0104a10869"))
        (      950000, uint256("0x338ae2fdf7d4c5d946960406b6f79e434ca5a4189432a6097447f40f0c03d44a"))
        (      960000, uint256("0xb97a873fb2334cd87428a7ca64959f55f2cf5bb0438101500ef4a397a51a84b8"))
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
