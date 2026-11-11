#pragma once
#include <WS2tcpip.h>

// ============================================
// ライブリンク受信（エディタ → ゲーム、開発用）
//  ・1パケット = "ヘッダ\n本文"
//  ・64KB を超えるものは分割して届く：
//      "chunk:<メッセージID>:<番号>:<総数>\n<元パケットの一部>"
//    全部そろったら元のパケットに戻してから渡す
//  ・受け付けるのは bindAddr（既定 127.0.0.1）宛てだけ
// ============================================
class LiveLinkServer
{
public:
	// header = 1行目、body = 2行目以降
	using Handler = std::function<void(const std::string& header, const std::string& body)>;

	LiveLinkServer() = default;
	~LiveLinkServer() { Stop(); }
	LiveLinkServer(const LiveLinkServer&) = delete;
	LiveLinkServer& operator=(const LiveLinkServer&) = delete;

	bool Start(unsigned short port, const char* bindAddr = "127.0.0.1")
	{
		Stop();

		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
		m_wsa = true;

		m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_sock == INVALID_SOCKET) { Stop(); return false; }

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if (inet_pton(AF_INET, bindAddr ? bindAddr : "127.0.0.1", &addr.sin_addr) != 1)
			inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			OutputDebugStringA("[LiveLink] bind failed\n");
			Stop();
			return false;
		}

		u_long nonblock = 1;
		ioctlsocket(m_sock, FIONBIO, &nonblock);

		// 大きいデータが一度に来ても取りこぼさないよう、受信バッファを広げる
		int rcvBuf = 4 * 1024 * 1024;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBuf, sizeof(rcvBuf));

		m_buf.resize(65536);
		char msg[96];
		sprintf_s(msg, "[LiveLink] started (port %u)\n", (unsigned)port);
		OutputDebugStringA(msg);
		return true;
	}

	void Stop()
	{
		if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
		if (m_wsa) { WSACleanup(); m_wsa = false; }
		m_partials.clear();
	}

	bool IsActive() const { return m_sock != INVALID_SOCKET; }

	// 受信済みのものを全部 handler に渡す。毎フレーム呼ぶ
	void Poll(const Handler& handler)
	{
		if (m_sock == INVALID_SOCKET) return;

		while (true)
		{
			sockaddr_in from = {};
			int fromLen = sizeof(from);
			const int recvd = recvfrom(m_sock, m_buf.data(), (int)m_buf.size(), 0,
				(sockaddr*)&from, &fromLen);
			if (recvd <= 0) break;

			std::string packet(m_buf.data(), (size_t)recvd);
			if (packet.rfind("chunk:", 0) == 0)
			{
				std::string whole;
				if (AddChunk(packet, whole)) Dispatch(whole, handler);
			}
			else
			{
				Dispatch(packet, handler);
			}
		}

		DropStalePartials();
	}

private:
	struct Partial
	{
		std::vector<std::string> parts;
		std::vector<bool> got;
		int received = 0;
		std::chrono::steady_clock::time_point last;
	};

	static void Dispatch(const std::string& packet, const Handler& handler)
	{
		const size_t nl = packet.find('\n');
		if (nl == std::string::npos) return;
		handler(packet.substr(0, nl), packet.substr(nl + 1));
	}

	// 分割パケットを1つ受け取る。そろったら out に元のパケットを入れて true
	bool AddChunk(const std::string& packet, std::string& out)
	{
		const size_t nl = packet.find('\n');
		if (nl == std::string::npos) return false;

		// "chunk:<id>:<index>:<count>"
		const std::string head = packet.substr(6, nl - 6);
		const size_t c1 = head.find(':');
		const size_t c2 = (c1 == std::string::npos) ? std::string::npos : head.find(':', c1 + 1);
		if (c2 == std::string::npos) return false;

		const std::string id = head.substr(0, c1);
		int index = 0, count = 0;
		try
		{
			index = std::stoi(head.substr(c1 + 1, c2 - c1 - 1));
			count = std::stoi(head.substr(c2 + 1));
		}
		catch (...) { return false; }
		if (count <= 0 || count > kMaxChunks || index < 0 || index >= count) return false;

		Partial& p = m_partials[id];
		if ((int)p.parts.size() != count)
		{
			p.parts.assign(count, std::string());
			p.got.assign(count, false);
			p.received = 0;
		}
		p.last = std::chrono::steady_clock::now();
		if (!p.got[index])
		{
			p.parts[index] = packet.substr(nl + 1);
			p.got[index] = true;
			++p.received;
		}
		if (p.received < count) return false;

		out.clear();
		for (auto& s : p.parts) out += s;
		m_partials.erase(id);
		return true;
	}

	// 欠けたまま一定時間たった分割パケットは捨てる
	void DropStalePartials()
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_partials.begin(); it != m_partials.end();)
		{
			if (now - it->second.last > std::chrono::seconds(5))
			{
				OutputDebugStringA(("[LiveLink] dropped incomplete message " + it->first + "\n").c_str());
				it = m_partials.erase(it);
			}
			else ++it;
		}
	}

	static constexpr int kMaxChunks = 256;   // 256 × 約60KB ≒ 15MB まで

	SOCKET m_sock = INVALID_SOCKET;
	bool   m_wsa = false;
	std::vector<char> m_buf;
	std::unordered_map<std::string, Partial> m_partials;
};
