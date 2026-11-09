#pragma once

#include "TransferTypes.hpp"
#include "IFileSystem.hpp"
#include "BufferRing.hpp"

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

/**
 * @brief High-performance asynchronous file transfer engine operating in service mode.
 *
 * All operations run as background service jobs added via addTransferJob().
 * Each job maintains its own FolderScanProgress, TransferProgress, and active items list
 * (completed items are pruned, maintaining only actual active/pending items).
 */
class TransferManager {
public:
    explicit TransferManager(std::shared_ptr<IFileSystem> fs = nullptr,
                             TransferOptions options = TransferOptions());
    virtual ~TransferManager();

    // Non-copyable
    TransferManager(const TransferManager&) = delete;
    TransferManager& operator=(const TransferManager&) = delete;

    // --- Service Lifecycle ---
    bool startService();
    void stopService();

    // --- Dynamic Job Management ---
    // Add a transfer job with individual source and destination directory
    virtual uint64_t addTransferJob(const std::wstring& sourcePath, const std::wstring& destinationDir, TransferMode mode = TransferMode::COPY);
    uint64_t addDynamicJob(const std::wstring& sourcePath, const std::wstring& destinationDir, TransferMode mode = TransferMode::COPY) {
        return addTransferJob(sourcePath, destinationDir, mode);
    }
    std::vector<uint64_t> addTransferJobs(const std::vector<std::wstring>& sources, const std::wstring& destinationDir, TransferMode mode = TransferMode::COPY);

    // Wait for a specific job to finish (completed or cancelled)
    bool waitForJob(uint64_t jobId);

    // Cancel a specific job
    bool cancelJob(uint64_t jobId);

    // Pause or resume a specific job
    bool pauseJob(uint64_t jobId, bool pause = true);

    // Global service wait, pause and resume
    void wait();
    void pause();
    void resume();

    // Job state queries
    bool isJobCompleted(uint64_t jobId) const;
    bool isJobCancelled(uint64_t jobId) const;
    bool isJobPaused(uint64_t jobId) const;

    // Critical I/O recovery
    bool recoverCriticalIo(bool retry);
    std::wstring getIoErrorReason() const;

    // Buffer ring configuration
    void setBufferRingSizeMB(size_t sizeMB, size_t chunkSize = 1024 * 1024);
    size_t getBufferRingSizeMB() const;
    size_t getBufferRingCapacityBytes() const;

    // Inspection (thread-safe)
    bool getJob(uint64_t jobId, TransferJobRequest& outJob) const;
    std::vector<TransferJobRequest> getJobs() const;

    bool getJobProgress(uint64_t jobId, TransferProgress& outProgress) const;
    std::vector<TransferProgress> getJobProgresses() const;

    bool getFolderScanProgress(uint64_t jobId, FolderScanProgress& outProgress) const;
    std::vector<FolderScanProgress> getFolderScanProgresses() const;

    std::vector<TransferQuestion> getActiveQuestions() const;

    // Per-job remembered user answers
    void setAnswerForJob(uint64_t jobId, QuestionType type, QuestionAnswer answer);
    std::map<QuestionType, QuestionAnswer> getRememberedAnswersForJob(uint64_t jobId) const;
    void clearRememberedAnswersForJob(uint64_t jobId);

    void setAnswerForType(QuestionType type, QuestionAnswer answer) {
        std::lock_guard<std::mutex> lock(question_mutex_);
        remembered_answers_[type] = answer;
    }

    bool answerQuestion(uint64_t questionId, QuestionAnswer answer, bool rememberForType = false, bool rememberForAllJobs = false);

    // Callbacks configuration
    void setTransferProgressCallback(std::function<void(const TransferItemProgress& currentItem, const TransferProgress& jobProgress)> cb) {
        item_transfer_progress_cb_ = std::move(cb);
    }
    void setProgressCallback(std::function<void(const TransferItemProgress& currentItem, const TransferProgress& jobProgress)> cb) {
        setTransferProgressCallback(std::move(cb));
    }
    void setScanProgressCallback(std::function<void(const FolderScanProgress&)> cb) {
        scan_progress_cb_ = std::move(cb);
    }
    void setQuestionCallback(std::function<QuestionAnswer(const TransferQuestion&, bool&)> cb) {
        question_cb_ = std::move(cb);
    }

    void setFileSystem(std::shared_ptr<IFileSystem> fs) { fs_ = fs; }
    std::shared_ptr<IFileSystem> getFileSystem() const { return fs_; }

    static bool isCriticalIoError(int err);
    static std::wstring generateUniqueRenamedPath(IFileSystem* fs, const std::wstring& originalPath);

protected:
    // Virtual callbacks for derived UI classes (e.g. far2l dialogs/TUI)
    // Primary callback: provides current item progress and the whole job progress directly
    virtual void onTransferProgress(const TransferItemProgress& currentItem, const TransferProgress& jobProgress);
    virtual void onFolderScanProgress(const FolderScanProgress& scan);
    virtual QuestionAnswer onQuestion(const TransferQuestion& question, bool& rememberForType);

private:
    std::shared_ptr<IFileSystem> fs_;
    TransferOptions options_;
    std::unique_ptr<BufferRing> buffer_ring_;

    // Threading
    std::vector<std::thread> reader_threads_;
    std::vector<std::thread> writer_threads_;
    std::thread discovery_thread_;

    std::atomic<bool> service_running_{false};
    std::atomic<bool> service_paused_{false};

    // Jobs registry: job_id -> TransferJobRequest
    mutable std::mutex job_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<TransferJobRequest>> jobs_;
    std::deque<std::shared_ptr<TransferJobRequest>> pending_job_requests_;
    std::atomic<uint64_t> next_job_id_{1};

    // Item queues for workers
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_items_;
    std::condition_variable cv_sources_;
    std::condition_variable cv_job_done_;
    std::condition_variable cv_all_done_;
    std::deque<std::shared_ptr<TransferItem>> pending_items_;
    std::atomic<uint64_t> next_item_id_{1};

    // Critical I/O pause & recovery state
    std::atomic<bool> io_error_paused_{false};
    std::wstring io_error_reason_;
    mutable std::mutex io_error_mutex_;
    std::condition_variable cv_io_recover_;
    uint64_t active_critical_question_id_{0};

    // Question Pool
    mutable std::mutex question_mutex_;
    std::condition_variable cv_questions_;
    std::vector<TransferQuestion> active_questions_;
    std::map<QuestionType, QuestionAnswer> remembered_answers_;
    std::atomic<uint64_t> next_question_id_{1};

    // Fast path statistics
    std::atomic<uint64_t> cow_bytes_{0};
    std::atomic<uint64_t> sparse_bytes_skipped_{0};
    std::atomic<uint64_t> mmap_bytes_{0};
    std::atomic<bool> had_critical_io_error_{false};

    // Callbacks
    std::function<void(const TransferItemProgress& currentItem, const TransferProgress& jobProgress)> item_transfer_progress_cb_;
    std::function<void(const FolderScanProgress&)> scan_progress_cb_;
    std::function<QuestionAnswer(const TransferQuestion&, bool&)> question_cb_;

    // Thread worker loops
    void discoveryWorker();
    void readerWorker(size_t threadIndex);
    void writerWorker(size_t threadIndex);

    // Item processing algorithms
    bool processSingleItem(std::shared_ptr<TransferItem> item);
    bool transferFile(std::shared_ptr<TransferItem> item);
    bool transferDirectory(std::shared_ptr<TransferItem> item);
    bool transferSymlink(std::shared_ptr<TransferItem> item);

    // Fast paths
    bool tryCoWTransfer(std::shared_ptr<TransferItem> item, int srcFd, int dstFd);
    bool transferSparseFile(std::shared_ptr<TransferItem> item, int srcFd, int dstFd);
    bool transferSmallFileMmap(std::shared_ptr<TransferItem> item, int srcFd, int dstFd);
    bool transferChunked(std::shared_ptr<TransferItem> item, int srcFd, int dstFd);

    // Questions and I/O handling
    QuestionAnswer handleCriticalIo(const std::wstring& path, int errCode, const std::wstring& opName);
    QuestionAnswer resolveQuestion(std::shared_ptr<TransferItem> item, QuestionType type, const std::wstring& msg, int err = 0);

    // Symlink relative path handling
    std::wstring recomputeRelativeSymlink(const std::wstring& rawTarget,
                                         const std::wstring& symlinkSrcPath,
                                         const std::wstring& symlinkDstPath,
                                         const std::wstring& rootSrcTree);

    // Progress Pitching directly from worker threads
    void pitchItemProgress(std::shared_ptr<TransferJobRequest> job, std::shared_ptr<TransferItem> item, bool forceFinal = false);
    void finishItem(std::shared_ptr<TransferJobRequest> job, std::shared_ptr<TransferItem> item);
    void checkJobCompletion(std::shared_ptr<TransferJobRequest> job);
};
