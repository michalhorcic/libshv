#ifndef SHV_CORE_UTILS_SHVJOURNALCOMMON_H
#define SHV_CORE_UTILS_SHVJOURNALCOMMON_H

#include "../shvcoreglobal.h"

#include <string>
#include <regex>

namespace shv {
namespace chainpack { class RpcValue; }
namespace core {
namespace utils {

class ShvJournalEntry;
class ShvGetLogParams;

class SHVCORE_DECL_EXPORT AbstractShvJournal
{
public:
	static const int DEFAULT_GET_LOG_RECORD_COUNT_LIMIT;

	static const char *KEY_NAME;
	static const char *KEY_RECORD_COUNT;
	static const char *KEY_PATHS_DICT;

public:
	virtual ~AbstractShvJournal();

	virtual void append(const ShvJournalEntry &entry) = 0;
	virtual shv::chainpack::RpcValue getLog(const ShvGetLogParams &params) = 0;
	virtual shv::chainpack::RpcValue getSnapShotMap();
};

} // namespace utils
} // namespace core
} // namespace shv

#endif // SHV_CORE_UTILS_SHVJOURNALCOMMON_H
