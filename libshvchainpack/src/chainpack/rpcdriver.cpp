#include "rpcdriver.h"
#include "metatypes.h"
#include "chainpackprotocol.h"
#include "cponprotocol.h"
#include "exception.h"

#include <necrolog.h>

#include <sstream>
#include <iostream>

#define logRpc() nCDebug("rpc")

namespace shv {
namespace chainpack {

int RpcDriver::s_defaultRpcTimeout = 5000;

RpcDriver::RpcDriver()
{
}

RpcDriver::~RpcDriver()
{
}

void RpcDriver::sendMessage(const RpcValue &msg)
{
	using namespace std;
	//shvLogFuncFrame() << msg.toStdString();
	std::ostringstream os_packed_data;
	switch (protocolVersion()) {
	/*
	case Json: {
		RpcValue::Map json_msg;
		RpcMessage rpc_msg(msg);
		RpcValue shv_path = rpc_msg.shvPath();
		json_msg["shvPath"] = shv_path;
		if(rpc_msg.isResponse()) {
			// response
			json_msg["id"] = rpc_msg.id();
			RpcResponse resp(rpc_msg);
			if(resp.isError())
				json_msg["error"] = resp.error().message();
			else
				json_msg["result"] = resp.result();
		}
		else {
			// request
			RpcRequest rq(rpc_msg);
			json_msg["id"] = rq.id();
			json_msg["method"] = rq.method();
			json_msg["params"] = rq.params();
			if(rpc_msg.isRequest())
				json_msg["id"] = rpc_msg.id();
		}
		shv::chainpack::CponProtocol::write(os_packed_data, json_msg);
		break;
	}
	*/
	case Cpon:
		CponProtocol::write(os_packed_data, msg);
		break;
	default:
		ChainPackProtocol::write(os_packed_data, msg);
		break;
	}
	std::string packed_data = os_packed_data.str();
	logRpc() << "send message: packed data: " << (packed_data.size() > 250? "<... long data ...>" :
				(protocolVersion() == Cpon? packed_data: Utils::toHex(packed_data)));

	enqueueDataToSend(Chunk{std::move(packed_data)});
}

void RpcDriver::sendRawData(std::string &&data)
{
	enqueueDataToSend(Chunk{std::move(data)});
}

void RpcDriver::sendRawData(RpcValue::MetaData &&meta_data, std::string &&data)
{
	using namespace std;
	//shvLogFuncFrame() << msg.toStdString();
	std::ostringstream os_packed_data;
	switch (protocolVersion()) {
	case Cpon:
		CponProtocol::writeMetaData(os_packed_data, meta_data);
		break;
	default:
		ChainPackProtocol::writeMetaData(os_packed_data, meta_data);
		break;
	}
	enqueueDataToSend(Chunk(os_packed_data.str(), std::move(data)));
}

void RpcDriver::enqueueDataToSend(RpcDriver::Chunk &&chunk_to_enqueue)
{
	/// LOCK_FOR_SEND lock mutex here in the multithreaded environment
	lockSendQueue();
	if(!chunk_to_enqueue.empty())
		m_chunkQueue.push_back(std::move(chunk_to_enqueue));
	if(!isOpen()) {
		nError() << "write data error, socket is not open!";
		return;
	}
	flush();
	writeQueue();
	/// UNLOCK_FOR_SEND unlock mutex here in the multithreaded environment
	unlockSendQueue();
}

void RpcDriver::writeQueue()
{
	if(m_chunkQueue.empty())
		return;
	logRpc() << "writePendingData(), queue len:" << m_chunkQueue.size();
	//static int hi_cnt = 0;
	const Chunk &chunk = m_chunkQueue[0];

	if(!m_topChunkHeaderWritten) {
		std::string protocol_version_data;
		{
			std::ostringstream os;
			ChainPackProtocol::writeUIntData(os, protocolVersion());
			protocol_version_data = os.str();
		}
		{
			std::ostringstream os;
			ChainPackProtocol::writeUIntData(os, chunk.size() + protocol_version_data.length());
			std::string packet_len_data = os.str();
			auto len = writeBytes(packet_len_data.data(), packet_len_data.length());
			if(len < 0)
				SHVCHP_EXCEPTION("Write socket error!");
			if(len < (int)packet_len_data.length())
				SHVCHP_EXCEPTION("Design error! Chunk length shall be always written at once to the socket");
		}
		{
			auto len = writeBytes(protocol_version_data.data(), protocol_version_data.length());
			if(len < 0)
				SHVCHP_EXCEPTION("Write socket error!");
			if(len != 1)
				SHVCHP_EXCEPTION("Design error! Protocol version shall be always written at once to the socket");
		}
		m_topChunkHeaderWritten = true;
	}
	if(m_topChunkBytesWrittenSoFar < chunk.metaData.size()) {
		m_topChunkBytesWrittenSoFar += writeBytes_helper(chunk.metaData, m_topChunkBytesWrittenSoFar, chunk.metaData.size() - m_topChunkBytesWrittenSoFar);
	}
	if(m_topChunkBytesWrittenSoFar >= chunk.metaData.size()) {
		m_topChunkBytesWrittenSoFar += writeBytes_helper(chunk.data
														 , m_topChunkBytesWrittenSoFar - chunk.metaData.size()
														 , chunk.data.size() - (m_topChunkBytesWrittenSoFar - chunk.metaData.size()));
		//logRpc() << "writeQueue - data len:" << chunk.length() << "start index:" << m_topChunkBytesWrittenSoFar << "bytes written:" << len << "remaining:" << (chunk.length() - m_topChunkBytesWrittenSoFar - len);
	}
	if(m_topChunkBytesWrittenSoFar == chunk.size()) {
		m_topChunkHeaderWritten = false;
		m_topChunkBytesWrittenSoFar = 0;
		m_chunkQueue.pop_front();
	}
}

int64_t RpcDriver::writeBytes_helper(const std::string &str, size_t from, size_t length)
{
	auto len = writeBytes(str.data() + from, length);
	if(len < 0)
		SHVCHP_EXCEPTION("Write socket error!");
	if(len == 0)
		SHVCHP_EXCEPTION("Design error! At least 1 byte of data shall be always written to the socket");
	return len;
}

void RpcDriver::onBytesRead(std::string &&bytes)
{
	logRpc() << bytes.length() << "bytes of data read";
	m_readData += std::string(std::move(bytes));
	while(true) {
		int len = processReadData(m_readData);
		//shvInfo() << len << "bytes of" << m_readData.size() << "processed";
		if(len > 0)
			m_readData = m_readData.substr(len);
		else
			break;
	}
}

int RpcDriver::processReadData(const std::string &read_data)
{
	logRpc() << __FUNCTION__ << "data len:" << read_data.length();

	using namespace shv::chainpack;

	std::istringstream in(read_data);

	bool ok;
	uint64_t chunk_len = ChainPackProtocol::readUIntData(in, &ok);
	if(!ok)
		return 0;

	size_t read_len = (size_t)in.tellg() + chunk_len;

	ProtocolVersion protocol_version = (ProtocolVersion)ChainPackProtocol::readUIntData(in, &ok);
	if(!ok)
		return 0;

	logRpc() << "\t chunk len:" << chunk_len << "read_len:" << read_len << "stream pos:" << in.tellg();
	if(read_len > read_data.length())
		return 0;

	//		 << "reading bytes:" << (protocolVersion() == Cpon? read_data: shv::core::Utils::toHex(read_data));

	RpcValue::MetaData meta_data;
	size_t meta_data_end_pos;
	switch (protocol_version) {
	/*
	case Json: {
		msg = CponProtocol::read(read_data, (size_t)in.tellg());
		if(msg.isMap()) {
			nError() << "JSON message cannot be translated to ChainPack";
		}
		else {
			const RpcValue::Map &map = msg.toMap();
			unsigned id = map.value("id").toUInt();
			RpcValue::String method = map.value("method").toString();
			RpcValue result = map.value("result");
			RpcValue error = map.value("error");
			RpcValue shv_path = map.value("shvPath");
			if(method.empty()) {
				// response
				RpcResponse resp;
				resp.setShvPath(shv_path);
				resp.setId(id);
				if(result.isValid())
					resp.setResult(result);
				else if(error.isValid())
					resp.setError(error.toIMap());
				else
					nError() << "JSON RPC response must contain response or error field";
				msg = resp.value();
			}
			else {
				// request
				RpcRequest rq;
				rq.setShvPath(shv_path);
				rq.setMethod(std::move(method));
				rq.setParams(map.value("params"));
				if(id > 0)
					rq.setId(id);
				msg = rq.value();
			}
		}
		break;
	}
	*/
	case Cpon: {
		meta_data = CponProtocol::readMetaData(read_data, (size_t)in.tellg(), &meta_data_end_pos);
		break;
	}
	case ChainPack: {
		meta_data = ChainPackProtocol::readMetaData(in);
		meta_data_end_pos = (size_t)in.tellg();
		break;
	}
	default:
		nError() << "Throwing away message with unknown protocol version:" << protocol_version;
		break;
	}
	onRpcDataReceived(protocol_version, std::move(meta_data), read_data, meta_data_end_pos, read_len - meta_data_end_pos);
	return read_len;
}

void RpcDriver::onRpcDataReceived(ProtocolVersion protocol_version, RpcValue::MetaData &&md, const std::string &data, size_t start_pos, size_t data_len)
{
	(void)data_len;
	RpcValue msg;
	switch (protocol_version) {
	case Cpon:
		msg = CponProtocol::read(data, start_pos);
		break;
	case ChainPack: {
		std::istringstream in(data);
		in.seekg(start_pos);
		msg = ChainPackProtocol::read(in);
		break;
	}
	default:
		nError() << "Throwing away message with unknown protocol version:" << protocol_version;
		break;
	}
	msg.setMetaData(std::move(md));
	onRpcValueReceived(msg);
}

void RpcDriver::onRpcValueReceived(const RpcValue &msg)
{
	logRpc() << "\t message received:" << msg.toCpon();
	//logLongFiles() << "\t emitting message received:" << msg.dumpText();
	if(m_messageReceivedCallback)
		m_messageReceivedCallback(msg);
}

} // namespace chainpack
} // namespace shv