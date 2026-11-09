#pragma once

#include "TransferManager.hpp"
#include "OsdPanel.hpp"
#include "formats.hpp"

class UITransferManager: public TransferManager {
public:
    UITransferManager(OsdPanel* _osd, std::shared_ptr<IFileSystem> fs, TransferOptions opts)
        : TransferManager(std::move(fs), opts), osd(_osd) {}

	virtual uint64_t addTransferJob(const std::wstring& sourcePath, const std::wstring& destinationDir, TransferMode mode = TransferMode::COPY) override;

protected:
    void onTransferProgress(const TransferItemProgress& currentItem, const TransferProgress& jobProgress) override;
    void onFolderScanProgress(const FolderScanProgress& scan) override;
    QuestionAnswer onQuestion(const TransferQuestion& q, bool& rememberForType) override;

private:
	OsdPanel* osd;
	std::unordered_map<uint64_t /* jobId */, int /* rowId */> jobToRow;
};
