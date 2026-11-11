#include "KdBinaryCacheUtil.h"
#include <windows.h>
#include <compressapi.h>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "Cabinet.lib")

namespace KdBinaryCache
{
	namespace
	{
		//----------------------------------------------------------
		// コンテナヘッダ
		//
		// 'KDBC' … このファイルがKdBinaryCacheのコンテナである事の目印。
		// これが無いと、拡張子だけ同じ別物(例: 素のDDSファイル)を読んだ時に
		// 先頭バイトをサイズとして解釈してしまい、巨大なallocationで
		// std::bad_alloc になる。
		//
		// version … フォーマットを変えた時にインクリメントする。
		// 古いキャッシュは「読めない」と判定され、自動的に作り直される。
		//----------------------------------------------------------
		constexpr uint32_t kContainerMagic = 0x4342444B;	// 'K','D','B','C' (リトルエンディアン)
		constexpr uint32_t kContainerVersion = 1;

		// magic(4) + version(4) + isCompressed(1) + uncompressedSize(8) + compressedSize(8)
		constexpr uint64_t kHeaderSize = 4 + 4 + 1 + 8 + 8;
	}

	bool CompressBuffer(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst)
	{
		if (src.empty())
		{
			dst.clear();
			return true;
		}

		COMPRESSOR_HANDLE compressor = nullptr;
		if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor))
		{
			return false;
		}

		// 必要なバッファサイズを問い合わせる(この呼び出し自体は失敗して良い)
		SIZE_T compressedSize = 0;
		Compress(compressor, const_cast<uint8_t*>(src.data()), src.size(), nullptr, 0, &compressedSize);

		// 問い合わせ結果が異常な場合は確保せず失敗にする(呼び出し側は非圧縮で保存する)
		std::vector<uint8_t> buffer;
		if (!TryResizeVector(buffer, static_cast<uint64_t>(compressedSize)))
		{
			CloseCompressor(compressor);
			return false;
		}

		SIZE_T actualSize = 0;
		const BOOL ok = Compress(compressor, const_cast<uint8_t*>(src.data()), src.size(),
			buffer.data(), buffer.size(), &actualSize);

		CloseCompressor(compressor);

		if (!ok || actualSize > buffer.size()) { return false; }

		buffer.resize(actualSize);
		dst = std::move(buffer);
		return true;
	}

	bool DecompressBuffer(const std::vector<uint8_t>& src, size_t uncompressedSize, std::vector<uint8_t>& dst)
	{
		if (uncompressedSize == 0)
		{
			dst.clear();
			return true;
		}

		// 展開後サイズが上限を超えるならデータが壊れているとみなす
		std::vector<uint8_t> buffer;
		if (!TryResizeVector(buffer, static_cast<uint64_t>(uncompressedSize))) { return false; }

		DECOMPRESSOR_HANDLE decompressor = nullptr;
		if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor))
		{
			return false;
		}

		SIZE_T actualSize = 0;
		const BOOL ok = Decompress(decompressor, const_cast<uint8_t*>(src.data()), src.size(),
			buffer.data(), buffer.size(), &actualSize);

		CloseDecompressor(decompressor);

		if (!ok || actualSize > buffer.size()) { return false; }

		buffer.resize(actualSize);
		dst = std::move(buffer);
		return true;
	}

	bool SaveCompressedFile(const std::string& path, const std::vector<uint8_t>& buffer)
	{
		std::vector<uint8_t> compressed;
		const bool compressedOk = CompressBuffer(buffer, compressed);

		// 圧縮に失敗した場合は非圧縮のまま保存する(致命的にしない)
		const std::vector<uint8_t>& payload = compressedOk ? compressed : buffer;
		const uint64_t uncompressedSize = buffer.size();
		const uint64_t compressedSize = payload.size();
		const uint8_t  isCompressed = compressedOk ? 1 : 0;

		//----------------------------------------------------------
		// 一旦一時ファイルへ書き出してから、最後にリネームで置き換える。
		// 直接書き込むと、書き込み途中でアプリが落ちた場合に
		// 「途中まで書かれた壊れたキャッシュ」が残ってしまうため
		//----------------------------------------------------------
		const std::string tmpPath = path + ".tmp" + std::to_string(GetCurrentThreadId());

		{
			std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
			if (!ofs) { return false; }

			ofs.write(reinterpret_cast<const char*>(&kContainerMagic), sizeof(kContainerMagic));
			ofs.write(reinterpret_cast<const char*>(&kContainerVersion), sizeof(kContainerVersion));
			ofs.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));
			ofs.write(reinterpret_cast<const char*>(&uncompressedSize), sizeof(uncompressedSize));
			ofs.write(reinterpret_cast<const char*>(&compressedSize), sizeof(compressedSize));
			if (!payload.empty())
			{
				ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
			}

			if (!ofs) { ofs.close(); std::error_code ec; std::filesystem::remove(tmpPath, ec); return false; }
		}

		std::error_code ec;
		std::filesystem::rename(tmpPath, path, ec);
		if (ec)
		{
			std::filesystem::remove(tmpPath, ec);
			return false;
		}

		return true;
	}

	bool LoadCompressedFile(const std::string& path, std::vector<uint8_t>& outBuffer)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs) { return false; }

		// 実ファイルサイズを取得しておく(ヘッダ内のサイズ値と突き合わせるため)
		const std::streamoff fileSizeRaw = ifs.tellg();
		if (fileSizeRaw < 0) { return false; }
		const uint64_t fileSize = static_cast<uint64_t>(fileSizeRaw);

		if (fileSize < kHeaderSize) { return false; }

		ifs.seekg(0, std::ios::beg);
		if (!ifs) { return false; }

		uint32_t magic = 0;
		uint32_t version = 0;
		uint8_t  isCompressed = 0;
		uint64_t uncompressedSize = 0;
		uint64_t compressedSize = 0;

		ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
		ifs.read(reinterpret_cast<char*>(&isCompressed), sizeof(isCompressed));
		ifs.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(uncompressedSize));
		ifs.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));
		if (!ifs) { return false; }

		//----------------------------------------------------------
		// [重要] 確保する前に必ず検証する
		//----------------------------------------------------------
		// 別形式のファイル(素のDDS等)を掴んだ場合はここで弾かれる
		if (magic != kContainerMagic) { return false; }
		// 旧バージョンのキャッシュは読まない(呼び出し側が作り直す)
		if (version != kContainerVersion) { return false; }
		// ヘッダのサイズと実ファイルサイズが一致しない = 壊れている / 切り詰められている
		if (compressedSize != fileSize - kHeaderSize) { return false; }
		// 明らかに異常なサイズを弾く
		if (uncompressedSize > kMaxBufferSize) { return false; }
		// 非圧縮フラグなのにサイズが食い違うのも不整合
		if (!isCompressed && uncompressedSize != compressedSize) { return false; }

		std::vector<uint8_t> payload;
		if (!TryResizeVector(payload, compressedSize)) { return false; }

		if (!payload.empty())
		{
			ifs.read(reinterpret_cast<char*>(payload.data()), payload.size());
			// 要求したバイト数を実際に読めたか確認する
			if (!ifs || static_cast<uint64_t>(ifs.gcount()) != compressedSize) { return false; }
		}

		if (!isCompressed)
		{
			outBuffer = std::move(payload);
			return true;
		}

		return DecompressBuffer(payload, static_cast<size_t>(uncompressedSize), outBuffer);
	}
}