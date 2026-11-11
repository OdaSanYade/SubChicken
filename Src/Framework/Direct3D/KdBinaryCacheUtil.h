#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <new>

//==========================================================================
// KdBinaryCacheUtil
//
// .kdmcache(モデル)/.kdtcache.dds(テクスチャ)など、独自バイナリキャッシュで
// 共通して使う「汎用圧縮」と「メモリ上でのバイナリ読み書き」をまとめたヘルパー。
//
// 圧縮にはWindows標準のCompression API(<compressapi.h>、追加ライブラリ不要)を
// 使う。中身(頂点・アニメーション・BC圧縮テクスチャ等)は一切変更せず、
// バイト列全体をロスレス圧縮するだけなので画質やモデル形状には影響しない。
//
// [重要] キャッシュファイルは「壊れている / 別形式である」可能性が常にある。
//   ・旧バージョンが書いたフォーマット違いのファイル
//   ・書き込み途中でアプリが終了して切り詰められたファイル
//   ・拡張子が同じだけの無関係なファイル(例: .dds キャッシュ位置に本物のDDS)
// これらを無検証で読むと、先頭バイトを「サイズ」として解釈してしまい、
// 巨大なallocationで std::bad_alloc になる。そのため本ヘッダ/実装では
// 「確保する前に必ず検証する」を徹底し、異常時は例外ではなく false を返す。
//==========================================================================
namespace KdBinaryCache
{
	//----------------------------------------------------------
	// 安全上限
	// 1回の読み込みでこれを超えるサイズを要求されたら、データが壊れていると
	// みなして失敗扱いにする(正常なモデル/テクスチャキャッシュはこれより遥かに小さい)
	//----------------------------------------------------------
	inline constexpr uint64_t kMaxBufferSize = 1024ull * 1024ull * 1024ull;	// 1GiB

	// bad_allocを外へ投げずに確保を試みる(失敗時 false)
	template<typename T>
	inline bool TryResizeVector(std::vector<T>& v, uint64_t count)
	{
		// 要素数 → バイト数へ変換する際のオーバーフローを先に弾く
		if (count != 0 && count > kMaxBufferSize / sizeof(T)) { return false; }

		try
		{
			v.resize(static_cast<size_t>(count));
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		return true;
	}

	//----------------------------------------------------------
	// 汎用圧縮(ロスレス)
	//----------------------------------------------------------

	// src を圧縮して dst へ書き出す(失敗時 false。dstは触らない)
	bool CompressBuffer(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst);

	// src(CompressBufferで圧縮したバイト列)を、元のサイズ(uncompressedSize)へ
	// 展開して dst へ書き出す(失敗時 false)
	bool DecompressBuffer(const std::vector<uint8_t>& src, size_t uncompressedSize, std::vector<uint8_t>& dst);

	//----------------------------------------------------------
	// メモリ上でのバイナリ書き込み
	// (POD配列はmemcpy相当で一括書き込みする。仮想関数を持たないPOD構造体
	//  専用。書き込み後、圧縮してからファイルへ保存する用途を想定)
	//----------------------------------------------------------
	class ByteWriter
	{
	public:
		template<typename T>
		void WritePOD(const T& value)
		{
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
			m_buffer.insert(m_buffer.end(), p, p + sizeof(T));
		}

		void WriteString(const std::string& s)
		{
			WritePOD(static_cast<uint32_t>(s.size()));
			m_buffer.insert(m_buffer.end(), s.begin(), s.end());
		}

		template<typename T>
		void WriteVector(const std::vector<T>& v)
		{
			WritePOD(static_cast<uint32_t>(v.size()));
			if (!v.empty())
			{
				const uint8_t* p = reinterpret_cast<const uint8_t*>(v.data());
				m_buffer.insert(m_buffer.end(), p, p + sizeof(T) * v.size());
			}
		}

		const std::vector<uint8_t>& GetBuffer() const { return m_buffer; }

	private:
		std::vector<uint8_t> m_buffer;
	};

	//----------------------------------------------------------
	// メモリ上でのバイナリ読み込み(ByteWriterで書いた内容を読む)
	//
	// 残りバイト数の比較は必ず「引き算(m_size - m_pos)」で行う。
	// 「m_pos + 要求サイズ > m_size」と書くと、要求サイズが巨大な時に
	// 加算がオーバーフローして条件をすり抜けてしまうため。
	//----------------------------------------------------------
	class ByteReader
	{
	public:
		ByteReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

		// 残りバイト数
		size_t GetRemain() const { return m_size - m_pos; }

		template<typename T>
		bool ReadPOD(T& value)
		{
			if (sizeof(T) > GetRemain()) { return false; }
			memcpy(&value, m_data + m_pos, sizeof(T));
			m_pos += sizeof(T);
			return true;
		}

		bool ReadString(std::string& s)
		{
			uint32_t len = 0;
			if (!ReadPOD(len)) { return false; }
			if (len > GetRemain()) { return false; }

			s.assign(reinterpret_cast<const char*>(m_data + m_pos), len);
			m_pos += len;
			return true;
		}

		template<typename T>
		bool ReadVector(std::vector<T>& v)
		{
			uint32_t count = 0;
			if (!ReadPOD(count)) { return false; }

			//----------------------------------------------------------
			// [重要] resize は必ず範囲チェックの「後」に行う。
			// 先にresizeすると、壊れたデータでcountが巨大な値だった場合に
			// その場で bad_alloc になってしまう
			//----------------------------------------------------------
			// 要素数 → バイト数のオーバーフローを弾く
			if (count != 0 && count > GetRemain() / sizeof(T)) { return false; }

			const size_t bytes = sizeof(T) * static_cast<size_t>(count);

			v.clear();
			if (!TryResizeVector(v, count)) { return false; }

			if (bytes > 0)
			{
				memcpy(v.data(), m_data + m_pos, bytes);
				m_pos += bytes;
			}
			return true;
		}

	private:
		const uint8_t* m_data = nullptr;
		size_t         m_size = 0;
		size_t         m_pos = 0;
	};

	//----------------------------------------------------------
	// ファイルIO
	//----------------------------------------------------------

	// buffer を圧縮した上で、独自コンテナ形式でファイルへ保存する。
	//
	//  [magic 'KDBC' : 4][version : 4][isCompressed : 1]
	//  [uncompressedSize : 8][compressedSize : 8][payload : compressedSize]
	//
	// 圧縮に失敗した場合は非圧縮のまま(isCompressed == 0)保存する。
	// 書き込みは一時ファイルへ行ってから置き換えるため、途中で終了しても
	// 壊れかけのキャッシュが残らない。
	bool SaveCompressedFile(const std::string& path, const std::vector<uint8_t>& buffer);

	// SaveCompressedFileで保存したファイルを読み込み、展開したバイト列を返す。
	// マジック不一致・バージョン違い・サイズ不整合・確保失敗はすべて false を返す
	// (呼び出し側は元データからの通常ロードへフォールバックすること)
	bool LoadCompressedFile(const std::string& path, std::vector<uint8_t>& outBuffer);
}