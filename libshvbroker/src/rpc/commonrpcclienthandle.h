#pragma once

#include <shv/chainpack/rpcmessage.h>

namespace shv { namespace core { class StringView; }}

namespace shv {
namespace broker {
namespace rpc {

class CommonRpcClientHandle
{
public:
	struct Subscription
	{
		std::string localPath;
		std::string subscribedPath;
		std::string method;
		//bool isRelative = false;

		Subscription() {}
		Subscription(const std::string &local_path, const std::string &subscribed_path, const std::string &m);

		bool cmpSubscribed(const CommonRpcClientHandle::Subscription &o) const;
		bool match(const shv::core::StringView &shv_path, const shv::core::StringView &shv_method) const;
		std::string toString() const {return localPath + ':' + method;}
	};
public:
	CommonRpcClientHandle();
	virtual ~CommonRpcClientHandle();

	virtual int connectionId() const = 0;
	virtual bool isConnectedAndLoggedIn() const = 0;

	virtual Subscription createSubscription(const std::string &shv_path, const std::string &method) = 0;
	unsigned addSubscription(const Subscription &subs);
	//virtual bool removeSubscription(const std::string &shv_path, const std::string &method) = 0;
	bool removeSubscription(const Subscription &subs);
	int isSubscribed(const std::string &shv_path, const std::string &method) const;
	virtual std::string toSubscribedPath(const Subscription &subs, const std::string &abs_path) const = 0;
	size_t subscriptionCount() const {return m_subscriptions.size();}
	const Subscription& subscriptionAt(size_t ix) const {return m_subscriptions.at(ix);}
	bool rejectNotSubscribedSignal(const std::string &path, const std::string &method);

	virtual std::string loggedUserName() = 0;
	virtual bool isSlaveBrokerConnection() const = 0;
	virtual bool isMasterBrokerConnection() const = 0;

	virtual void sendRawData(const shv::chainpack::RpcValue::MetaData &meta_data, std::string &&data) = 0;
	virtual void sendMessage(const shv::chainpack::RpcMessage &rpc_msg) = 0;
protected:
	std::vector<Subscription> m_subscriptions;
};

}}}

