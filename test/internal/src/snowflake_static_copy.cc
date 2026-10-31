#include "common/data_types/snowflake.h"
#if defined(_WIN32)
#define SF_EXPORT extern "C" __declspec(dllexport)
#else
#define SF_EXPORT extern "C"
#endif
SF_EXPORT std::int64_t SnowflakeStaticCopyGenerate() noexcept { return bedrock::Snowflake::Generate(); }
