#pragma once
namespace mvp::specific_standings_candidate {
struct Policy {unsigned key,regular,slot,field;bool divided;const char* canonical;const char* contract_sha256;};
inline constexpr Policy kPolicies[]={
 {1,1,16,16,false,"ncaa-conf:821","fd70292bbc1287f12e984c0f684a776053bfe5faa3a8fbf07e8afcdba3aa4e4a"},
 {4,4,19,12,false,"ncaa-conf:25354","a1bbf2d7337d40baaef196ecd726bb49e012abdf043607ea19a1c9985848943f"},
 {10,10,23,4,false,"ncaa-conf:838","61ca55880026f1a03a7cb5bc7d156cf94b0e891a10945a8b5c1cf8e0b6d7f887"},
 {11,11,24,16,false,"ncaa-conf:911","7f05b15bbbc41919de59030a9e831986d15d90162c46485bfb95a600a6a69c3a"},
 {12,12,25,10,false,"ncaa-conf:818","c8b4f6f73a0552f32f1e810600f878642f4cc146bfd4d44d54d5bb49d2f17946"},
 {16,38,57,6,false,"ncaa-conf:820","aaa2caa636bd4992556523c89bcdfa939416f06bc71437574d3f46a7ce3d446a"},
 {17,39,58,8,false,"ncaa-conf:823","0ed0a0f294d6ddef6a71c8ae60c69bcb4b80c6fc31727559c00939f360c00a38"},
 {19,41,60,6,true,"ncaa-conf:837","0f16a79db6b3b9727f976843a0d154aa8b424e2c2dfc84d5facea62aaf9c9aad"},
 {21,43,62,4,false,"ncaa-conf:846","0f2183d75a46d6271081509267b3a92b3731ffafeaa32e64d37ac48d10b624e3"},
 {22,44,63,4,false,"ncaa-conf:865","d6f6b2c3ea2121537c771c4197b4c1bdc7d5850c55c0b720edb0cdcc8cd1b65f"},
 {23,45,64,6,false,"ncaa-conf:871","cb602f3a7f7d4eb23901ce5da6a748b2a418ca9501212cbabc5f945ad187540f"},
 {24,46,65,6,false,"ncaa-conf:875","d28811c7763ee39e338aa0ea548b618bfe49799082bd8e17fd6ef431f20c845e"},
 {26,48,67,8,false,"ncaa-conf:902","02e63b2a61dd9c165f3b2dc4d2206d268df75d06399566196e917e76ee1a0c87"},
 {29,51,70,8,false,"ncaa-conf:916","a2dc92368dcbb5f2fa6f3b5d8aee9f25931d309a869d838af799ccb2c5f23842"},
 {30,52,71,6,true,"ncaa-conf:920","53bcc683439ab8c0973a44e6389b63b4eed6eec811fc826ecef1b652da82629e"},
};
}
