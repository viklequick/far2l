#include "TransferManager.hpp"
#include "LocalFileSystem.hpp"
#include "WideUtils.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cerrno>

namespace fs = std::filesystem;

TransferManager::TransferManager(std::shared_ptr<IFileSystem> fs_impl, TransferOptions options)
    : fs_(fs_impl ? std::move(fs_impl) : std::make_shared<LocalFileSystem>()),
      options_(options) {
    if (options_.buffer_ring_size_mb >= 16 && options_.buffer_ring_size_mb <= 1024) {
        options_.buffer_ring_capacity = (options_.buffer_ring_size_mb * 1024 * 1024) / options_.chunk_size;
    }
    buffer_ring_ = std::make_unique<BufferRing>(options_.buffer_ring_capacity, options_.chunk_size);
}

TransferManager::~TransferManager() {
    stopService();
}

void TransferManager::setBufferRingSizeMB(size_t sizeMB, size_t chunkSize) {
    if (service_running_.load(std::memory_order_relaxed)) {
        return;
    }
    sizeMB = std::clamp<size_t>(sizeMB, 16, 1024);
    options_.buffer_ring_size_mb = sizeMB;
    options_.chunk_size = (chunkSize > 0) ? chunkSize : (1024 * 1024);
    options_.buffer_ring_capacity = (sizeMB * 1024 * 1024) / options_.chunk_size;
    buffer_ring_ = std::make_unique<BufferRing>(options_.buffer_ring_capacity, options_.chunk_size);
}

size_t TransferManager::getBufferRingSizeMB() const {
    return buffer_ring_ ? buffer_ring_->getTotalSizeMB() : options_.buffer_ring_size_mb;
}

size_t TransferManager::getBufferRingCapacityBytes() const {
    return buffer_ring_ ? buffer_ring_->getTotalSizeBytes() : (options_.buffer_ring_size_mb * 1024 * 1024);
}

bool TransferManager::startService() {
    if (service_running_.load(std::memory_order_acquire)) {
        return false;
    }

    service_running_.store(true, std::memory_order_release);
    service_paused_.store(false, std::memory_order_release);
    io_error_paused_.store(false, std::memory_order_release);

    // Spawn scanner/discovery worker thread
    discovery_thread_ = std::thread(&TransferManager::discoveryWorker, this);

    // Spawn Reader worker thread pool
    size_t readers = std::max<size_t>(1, options_.num_readers);
    for (size_t i = 0; i < readers; ++i) {
        reader_threads_.emplace_back(&TransferManager::readerWorker, this, i);
    }

    // Spawn Writer worker thread pool
    size_t writers = std::max<size_t>(1, options_.num_writers);
    for (size_t i = 0; i < writers; ++i) {
        writer_threads_.emplace_back(&TransferManager::writerWorker, this, i);
    }

    return true;
}

void TransferManager::stopService() {
    if (!service_running_.load(std::memory_order_acquire)) {
        return;
    }

    service_running_.store(false, std::memory_order_release);

    // Mark all remaining jobs as cancelled
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        for (auto& [id, j] : jobs_) {
            j->is_cancelled = true;
            j->progress.is_cancelled = true;
            j->active_items.clear();
        }
    }

    if (buffer_ring_) {
        buffer_ring_->shutdown();
    }

    // Wake up all condition variables
    cv_sources_.notify_all();
    cv_items_.notify_all();
    cv_questions_.notify_all();
    cv_io_recover_.notify_all();
    cv_job_done_.notify_all();
    cv_all_done_.notify_all();

    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }

    for (auto& t : reader_threads_) {
        if (t.joinable()) t.join();
    }
    reader_threads_.clear();

    for (auto& t : writer_threads_) {
        if (t.joinable()) t.join();
    }
    writer_threads_.clear();
}

uint64_t TransferManager::addTransferJob(const std::wstring& sourcePath, const std::wstring& destinationDir, TransferMode mode) {
    if (!service_running_.load(std::memory_order_acquire)) {
        startService();
    }

    if (!fs_->exists(destinationDir)) {
        fs_->createDirectories(destinationDir);
    }

    auto job = std::make_shared<TransferJobRequest>();
    job->job_id = next_job_id_++;
    job->source_path = sourcePath;
    job->destination_dir = destinationDir;
    job->mode = mode;

    job->scan_progress.job_id = job->job_id;
    job->scan_progress.folder_path = sourcePath;
    job->scan_progress.destination_dir = destinationDir;
    job->scan_progress.state = ScanState::SCANNING;

    job->progress.job_id = job->job_id;
    job->progress.source_path = sourcePath;
    job->progress.destination_dir = destinationDir;
    job->progress.mode = mode;
    job->progress.buffer_ring_size_mb = getBufferRingSizeMB();

    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        jobs_[job->job_id] = job;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_job_requests_.push_back(job);
    }
    cv_sources_.notify_all();

    return job->job_id;
}

std::vector<uint64_t> TransferManager::addTransferJobs(const std::vector<std::wstring>& sources, const std::wstring& destinationDir, TransferMode mode) {
    std::vector<uint64_t> ids;
    ids.reserve(sources.size());
    for (const auto& src : sources) {
        ids.push_back(addTransferJob(src, destinationDir, mode));
    }
    return ids;
}

bool TransferManager::waitForJob(uint64_t jobId) {
    std::unique_lock<std::mutex> lock(job_mutex_);
    cv_job_done_.wait(lock, [&]() {
        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return true;
        return it->second->is_completed || it->second->is_cancelled || !service_running_.load(std::memory_order_acquire);
    });
    return true;
}

bool TransferManager::cancelJob(uint64_t jobId) {
    std::shared_ptr<TransferJobRequest> job;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) {
            return false;
        }
        job = it->second;
        job->is_cancelled = true;
        job->progress.is_cancelled = true;
        job->active_items.clear();
    }

    // Emit final progress pitch with cancelled status
    TransferItemProgress cancelledProg;
    cancelledProg.source_path = job->source_path;
    cancelledProg.destination_path = job->destination_dir;
    cancelledProg.is_completed = false;
    onTransferProgress(cancelledProg, job->progress);

    cv_job_done_.notify_all();
    cv_all_done_.notify_all();
    return true;
}

bool TransferManager::pauseJob(uint64_t jobId, bool pause) {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) {
        return false;
    }
    it->second->is_paused = pause;
    it->second->progress.is_paused = pause;
    cv_items_.notify_all();
    return true;
}

void TransferManager::wait() {
    std::unique_lock<std::mutex> lock(job_mutex_);
    cv_all_done_.wait(lock, [&]() {
        if (!service_running_.load(std::memory_order_acquire)) return true;
        if (jobs_.empty()) return true;
        for (const auto& [id, j] : jobs_) {
            if (!j->is_completed && !j->is_cancelled) return false;
        }
        return true;
    });
}

void TransferManager::pause() {
    service_paused_.store(true, std::memory_order_release);
}

void TransferManager::resume() {
    service_paused_.store(false, std::memory_order_release);
    cv_items_.notify_all();
}

bool TransferManager::isJobCompleted(uint64_t jobId) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    return (it != jobs_.end()) && it->second->is_completed;
}

bool TransferManager::isJobCancelled(uint64_t jobId) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    return (it != jobs_.end()) && it->second->is_cancelled;
}

bool TransferManager::isJobPaused(uint64_t jobId) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    return (it != jobs_.end()) && it->second->is_paused;
}

std::wstring TransferManager::getIoErrorReason() const {
    std::lock_guard<std::mutex> lock(io_error_mutex_);
    return io_error_reason_;
}

bool TransferManager::isCriticalIoError(int err) {
    return (err == ENOSPC || err == EDQUOT || err == EROFS || err == EIO);
}

QuestionAnswer TransferManager::handleCriticalIo(const std::wstring& path, int errCode, const std::wstring& opName) {
    had_critical_io_error_.store(true, std::memory_order_release);
    io_error_paused_.store(true, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(io_error_mutex_);
        if (errCode == ENOSPC) {
            io_error_reason_ = L"No space left on device (ENOSPC) during " + opName + L" on " + path;
        } else if (errCode == EDQUOT) {
            io_error_reason_ = L"Disk quota exceeded (EDQUOT) during " + opName + L" on " + path;
        } else {
            io_error_reason_ = L"Critical device I/O error (" + std::to_wstring(errCode) + L") during " + opName + L" on " + path;
        }
    }

    TransferQuestion q;
    q.id = next_question_id_++;
    q.type = (errCode == ENOSPC) ? QuestionType::NO_SPACE_LEFT : QuestionType::CRITICAL_IO_ERROR;
    q.target_path = path;
    q.message = io_error_reason_;
    q.error_code = errCode;
    active_critical_question_id_ = q.id;

    bool remember = false;
    return onQuestion(q, remember);
}

bool TransferManager::recoverCriticalIo(bool retry) {
    if (retry) {
        {
            std::lock_guard<std::mutex> lock(io_error_mutex_);
            io_error_paused_.store(false, std::memory_order_release);
            io_error_reason_.clear();
            active_critical_question_id_ = 0;
        }
        cv_io_recover_.notify_all();
        cv_items_.notify_all();
        return true;
    } else {
        stopService();
        return false;
    }
}

bool TransferManager::getJob(uint64_t jobId, TransferJobRequest& outJob) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        outJob = *(it->second);
        return true;
    }
    return false;
}

std::vector<TransferJobRequest> TransferManager::getJobs() const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    std::vector<TransferJobRequest> result;
    result.reserve(jobs_.size());
    for (const auto& [id, j] : jobs_) {
        result.push_back(*j);
    }
    return result;
}

bool TransferManager::getJobProgress(uint64_t jobId, TransferProgress& outProgress) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        outProgress = it->second->progress;
        return true;
    }
    return false;
}

std::vector<TransferProgress> TransferManager::getJobProgresses() const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    std::vector<TransferProgress> list;
    list.reserve(jobs_.size());
    for (const auto& [id, j] : jobs_) {
        list.push_back(j->progress);
    }
    return list;
}

bool TransferManager::getFolderScanProgress(uint64_t jobId, FolderScanProgress& outProgress) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        outProgress = it->second->scan_progress;
        return true;
    }
    return false;
}

std::vector<FolderScanProgress> TransferManager::getFolderScanProgresses() const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    std::vector<FolderScanProgress> list;
    list.reserve(jobs_.size());
    for (const auto& [id, j] : jobs_) {
        list.push_back(j->scan_progress);
    }
    return list;
}

std::vector<TransferQuestion> TransferManager::getActiveQuestions() const {
    std::lock_guard<std::mutex> lock(question_mutex_);
    return active_questions_;
}

void TransferManager::setAnswerForJob(uint64_t jobId, QuestionType type, QuestionAnswer answer) {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        it->second->remembered_answers[type] = answer;
    }
}

std::map<QuestionType, QuestionAnswer> TransferManager::getRememberedAnswersForJob(uint64_t jobId) const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        return it->second->remembered_answers;
    }
    return {};
}

void TransferManager::clearRememberedAnswersForJob(uint64_t jobId) {
    std::lock_guard<std::mutex> lock(job_mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        it->second->remembered_answers.clear();
    }
}

bool TransferManager::answerQuestion(uint64_t questionId, QuestionAnswer answer, bool rememberForType, bool rememberForAllJobs) {
    std::lock_guard<std::mutex> lock(question_mutex_);
    auto it = std::find_if(active_questions_.begin(), active_questions_.end(),
                           [questionId](const TransferQuestion& q) { return q.id == questionId; });
    if (it == active_questions_.end()) {
        return false;
    }

    uint64_t qJobId = it->job_id;
    QuestionType qType = it->type;

    if (rememberForType) {
        if (rememberForAllJobs || qJobId == 0) {
            remembered_answers_[qType] = answer;
        } else {
            setAnswerForJob(qJobId, qType, answer);
        }
    }

    active_questions_.erase(it);
    cv_questions_.notify_all();
    return true;
}

// --- Thread Routines ---

void TransferManager::discoveryWorker() {
    while (service_running_.load(std::memory_order_acquire)) {
        std::shared_ptr<TransferJobRequest> job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_sources_.wait(lock, [&]() {
                return !pending_job_requests_.empty() || !service_running_.load(std::memory_order_acquire);
            });

            if (!service_running_.load(std::memory_order_acquire)) {
                break;
            }

            if (!pending_job_requests_.empty()) {
                job = pending_job_requests_.front();
                pending_job_requests_.pop_front();
            }
        }

        if (!job) continue;

        fs::path srcP(job->source_path);
        fs::path dstP(job->destination_dir);

        FileStat srcStat = fs_->statFile(job->source_path, false);
        if (!srcStat.exists) {
            job->scan_progress.state = ScanState::FAILED;
            job->scan_progress.is_completed = true;
            job->is_scan_completed = true;
            job->is_completed = true;
            job->progress.is_completed = true;
            onFolderScanProgress(job->scan_progress);
            cv_job_done_.notify_all();
            continue;
        }

        if (srcStat.is_regular || srcStat.is_symlink) {
            // Single regular file or symlink
            fs::path targetFilePath = dstP;
            if (fs_->isDirectory(job->destination_dir)) {
                targetFilePath = dstP / srcP.filename();
            }

            auto item = std::make_shared<TransferItem>();
            item->id = next_item_id_++;
            item->job_id = job->job_id;
            item->src_path = job->source_path;
            item->dst_path = targetFilePath.wstring();
            item->root_src = srcP.parent_path().wstring();
            item->root_dst = dstP.wstring();
            item->is_directory = false;
            item->is_symlink = srcStat.is_symlink;
            item->size = srcStat.size;

            item->progress.source_path = item->src_path;
            item->progress.destination_path = item->dst_path;
            item->progress.total_bytes = item->size;
            item->progress.is_directory = false;
            item->progress.is_symlink = item->is_symlink;

            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                job->active_items.push_back(item);
                job->scan_progress.scanned_files = 1;
                job->scan_progress.scanned_bytes = item->size;
                job->scan_progress.state = ScanState::COMPLETED;
                job->scan_progress.is_completed = true;
                job->progress.total_files = 1;
                job->progress.total_bytes = item->size;
                job->is_scan_completed = true;
            }

            onFolderScanProgress(job->scan_progress);

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                pending_items_.push_back(item);
            }
            cv_items_.notify_all();
        } else if (srcStat.is_directory) {
            // Directory tree scan
            fs::path targetFolder = dstP / srcP.filename();
            fs_->createDirectories(targetFolder.wstring());

            auto rootDirItem = std::make_shared<TransferItem>();
            rootDirItem->id = next_item_id_++;
            rootDirItem->job_id = job->job_id;
            rootDirItem->src_path = job->source_path;
            rootDirItem->dst_path = targetFolder.wstring();
            rootDirItem->root_src = job->source_path;
            rootDirItem->root_dst = targetFolder.wstring();
            rootDirItem->is_directory = true;

            rootDirItem->progress.source_path = rootDirItem->src_path;
            rootDirItem->progress.destination_path = rootDirItem->dst_path;
            rootDirItem->progress.is_directory = true;

            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                job->active_items.push_back(rootDirItem);
                job->scan_progress.scanned_folders = 1;
                job->progress.total_dirs = 1;
            }

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                pending_items_.push_back(rootDirItem);
            }
            cv_items_.notify_all();

            // Enumerate directory tree
            std::deque<fs::path> subdirs;
            subdirs.push_back(srcP);

            while (!subdirs.empty() && service_running_.load(std::memory_order_acquire) && !job->is_cancelled) {
                fs::path currDir = subdirs.front();
                subdirs.pop_front();

                std::vector<DirEntry> entries = fs_->listDirectory(currDir.wstring());

                for (const auto& entry : entries) {
                    if (entry.name == L"." || entry.name == L"..") continue;

                    fs::path rel = fs::relative(fs::path(entry.full_path), srcP);
                    fs::path itemDest = targetFolder / rel;

                    auto itm = std::make_shared<TransferItem>();
                    itm->id = next_item_id_++;
                    itm->job_id = job->job_id;
                    itm->src_path = entry.full_path;
                    itm->dst_path = itemDest.wstring();
                    itm->root_src = job->source_path;
                    itm->root_dst = targetFolder.wstring();
                    itm->is_directory = entry.is_directory;
                    itm->is_symlink = entry.is_symlink;
                    itm->size = entry.size;

                    itm->progress.source_path = itm->src_path;
                    itm->progress.destination_path = itm->dst_path;
                    itm->progress.total_bytes = itm->size;
                    itm->progress.is_directory = entry.is_directory;
                    itm->progress.is_symlink = entry.is_symlink;

                    {
                        std::lock_guard<std::mutex> lock(job_mutex_);
                        job->active_items.push_back(itm);
                        if (entry.is_directory) {
                            job->scan_progress.scanned_folders++;
                            job->progress.total_dirs++;
                        } else {
                            job->scan_progress.scanned_files++;
                            job->scan_progress.scanned_bytes += entry.size;
                            job->progress.total_files++;
                            job->progress.total_bytes += entry.size;
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        pending_items_.push_back(itm);
                    }
                    cv_items_.notify_all();

                    if (entry.is_directory) {
                        subdirs.push_back(entry.full_path);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                job->scan_progress.state = ScanState::COMPLETED;
                job->scan_progress.is_completed = true;
                job->is_scan_completed = true;
            }
            onFolderScanProgress(job->scan_progress);

            checkJobCompletion(job);
        }
    }
}

void TransferManager::readerWorker(size_t /*threadIndex*/) {
    while (service_running_.load(std::memory_order_acquire)) {
        std::shared_ptr<TransferItem> item;
        std::shared_ptr<TransferJobRequest> job;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_items_.wait(lock, [&]() {
                if (!service_running_.load(std::memory_order_acquire)) {
                    return true;
                }
                if (service_paused_.load(std::memory_order_acquire) || io_error_paused_.load(std::memory_order_acquire)) {
                    return false;
                }
                return !pending_items_.empty();
            });

            if (!service_running_.load(std::memory_order_acquire)) {
                break;
            }

            if (!pending_items_.empty()) {
                item = pending_items_.front();
                pending_items_.pop_front();
            }
        }

        if (!item) continue;

        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            auto it = jobs_.find(item->job_id);
            if (it != jobs_.end()) {
                job = it->second;
            }
        }

        if (!job || job->is_cancelled) {
            continue;
        }

        if (job->is_paused) {
            // Requeue paused job items
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_items_.push_back(item);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        processSingleItem(item);
    }
}

void TransferManager::writerWorker(size_t /*threadIndex*/) {
    while (service_running_.load(std::memory_order_acquire)) {
        if (!buffer_ring_ || buffer_ring_->isShutdown()) break;

        BufferChunk* chunk = buffer_ring_->acquireReadyChunk();
        if (!chunk) {
            break;
        }

        if (chunk->dst_fd >= 0 && chunk->bytes_valid > 0 && !chunk->is_eof) {
            ssize_t written = fs_->writeFile(chunk->dst_fd, chunk->buffer, chunk->bytes_valid, chunk->dst_offset);
            if (written < 0) {
                handleCriticalIo(L"destination_write", errno, L"writeFile");
                buffer_ring_->markChunkFailed(chunk, errno);
                continue;
            }
        }

        buffer_ring_->releaseCommittedChunk(chunk);
    }
}

bool TransferManager::processSingleItem(std::shared_ptr<TransferItem> item) {
    if (!item) return false;

    std::shared_ptr<TransferJobRequest> job;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = jobs_.find(item->job_id);
        if (it != jobs_.end()) job = it->second;
    }
    if (!job) return false;

    pitchItemProgress(job, item);

    bool ok = false;
    if (item->is_directory) {
        ok = transferDirectory(item);
    } else if (item->is_symlink) {
        ok = transferSymlink(item);
    } else {
        ok = transferFile(item);
    }

    finishItem(job, item);
    return ok;
}

bool TransferManager::transferDirectory(std::shared_ptr<TransferItem> item) {
    if (!fs_->exists(item->dst_path)) {
        if (!fs_->createDirectories(item->dst_path)) {
            return false;
        }
    }
    fs_->copyMetadata(item->src_path, item->dst_path, options_.preserve_ownership);
    return true;
}

bool TransferManager::transferSymlink(std::shared_ptr<TransferItem> item) {
    std::wstring linkTarget = fs_->readSymlink(item->src_path);
    if (linkTarget.empty()) {
        return false;
    }

    std::wstring adjustedTarget = recomputeRelativeSymlink(linkTarget, item->src_path, item->dst_path, item->root_src);

    if (fs_->exists(item->dst_path) || fs_->isSymlink(item->dst_path)) {
        QuestionAnswer ans = resolveQuestion(item, QuestionType::LINK_DESTINATION_EXISTS,
                                            L"Destination exists for symlink: " + item->dst_path);
        if (ans == QuestionAnswer::SKIP || ans == QuestionAnswer::SKIP_ALL) {
            return true;
        }
        fs_->removeFile(item->dst_path);
    }

    return fs_->createSymlink(adjustedTarget, item->dst_path);
}

std::wstring TransferManager::generateUniqueRenamedPath(IFileSystem* fs, const std::wstring& originalPath) {
    fs::path p(originalPath);
    std::wstring stem = p.stem().wstring();
    std::wstring ext = p.extension().wstring();
    fs::path parent = p.parent_path();

    for (int counter = 1; counter <= 9999; ++counter) {
        std::wstring candidate = (parent / (stem + L" (" + std::to_wstring(counter) + L")" + ext)).wstring();
        if (!fs->exists(candidate)) {
            return candidate;
        }
    }
    return originalPath;
}

std::wstring TransferManager::recomputeRelativeSymlink(const std::wstring& rawTarget,
                                                     const std::wstring& symlinkSrcPath,
                                                     const std::wstring& symlinkDstPath,
                                                     const std::wstring& rootSrcTree) {
    fs::path target(rawTarget);
    if (target.is_absolute()) {
        return rawTarget;
    }

    fs::path srcDir = fs::path(symlinkSrcPath).parent_path();
    fs::path resolvedTarget = fs::weakly_canonical(srcDir / target);

    fs::path rootTree(rootSrcTree);
    auto mismatch = std::mismatch(rootTree.begin(), rootTree.end(), resolvedTarget.begin());
    bool isInternal = (mismatch.first == rootTree.end());

    if (isInternal) {
        return rawTarget;
    }

    fs::path dstDir = fs::path(symlinkDstPath).parent_path();
    std::error_code ec;
    fs::path newRel = fs::relative(resolvedTarget, dstDir, ec);
    return ec ? rawTarget : newRel.wstring();
}

QuestionAnswer TransferManager::resolveQuestion(std::shared_ptr<TransferItem> item, QuestionType type, const std::wstring& msg, int err) {
    // 1. Check job-specific remembered answers first
    if (item && item->job_id > 0) {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = jobs_.find(item->job_id);
        if (it != jobs_.end()) {
            auto ansIt = it->second->remembered_answers.find(type);
            if (ansIt != it->second->remembered_answers.end()) {
                return ansIt->second;
            }
        }
    }

    // 2. Check global fallback answers
    {
        std::lock_guard<std::mutex> lock(question_mutex_);
        auto it = remembered_answers_.find(type);
        if (it != remembered_answers_.end()) {
            return it->second;
        }
    }

    // 3. Prompt user via callback
    TransferQuestion q;
    q.id = next_question_id_++;
    q.job_id = item ? item->job_id : 0;
    q.type = type;
    q.source_path = item ? item->src_path : L"";
    q.target_path = item ? item->dst_path : L"";
    q.message = msg;
    q.error_code = err;

    bool rememberForType = false;
    QuestionAnswer ans = onQuestion(q, rememberForType);

    if (rememberForType) {
        if (q.job_id > 0) {
            setAnswerForJob(q.job_id, type, ans);
        } else {
            setAnswerForType(type, ans);
        }
    }

    return ans;
}

bool TransferManager::transferFile(std::shared_ptr<TransferItem> item) {
    if (!item) return false;

    std::shared_ptr<TransferJobRequest> job;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = jobs_.find(item->job_id);
        if (it != jobs_.end()) job = it->second;
    }

    FileStat srcStat = fs_->statFile(item->src_path, false);
    FileStat dstStat = fs_->statFile(item->dst_path, false);
    bool dstExists = dstStat.exists;

    // Check collisions
    if (dstExists) {
        if (srcStat.dev == dstStat.dev && srcStat.ino == dstStat.ino && srcStat.ino != 0) {
            // Same source and target inode
            resolveQuestion(item, QuestionType::SAME_SOURCE_AND_TARGET, L"Cannot copy file onto itself");
            return true;
        }

        QuestionType qType = dstStat.is_read_only ? QuestionType::OVERWRITE_READ_ONLY_TARGET : QuestionType::OVERWRITE_EXISTING;
        QuestionAnswer decision = resolveQuestion(item, qType, L"Destination file already exists: " + item->dst_path);

        if (decision == QuestionAnswer::SKIP || decision == QuestionAnswer::SKIP_ALL) {
            return true;
        } else if (decision == QuestionAnswer::OVERWRITE_IF_NEWER) {
            bool srcIsNewer = (srcStat.mtime_sec > dstStat.mtime_sec) ||
                              (srcStat.mtime_sec == dstStat.mtime_sec && srcStat.mtime_nsec > dstStat.mtime_nsec);
            if (!srcIsNewer) return true;
        } else if (decision == QuestionAnswer::OVERWRITE_IF_DIFFERENT_SIZE) {
            if (srcStat.size == dstStat.size) return true;
        } else if (decision == QuestionAnswer::OVERWRITE_IF_NEWER_OR_DIFFERENT_SIZE) {
            bool srcIsNewer = (srcStat.mtime_sec > dstStat.mtime_sec) ||
                              (srcStat.mtime_sec == dstStat.mtime_sec && srcStat.mtime_nsec > dstStat.mtime_nsec);
            if (!srcIsNewer && srcStat.size == dstStat.size) return true;
        } else if (decision == QuestionAnswer::AUTO_RENAME) {
            item->dst_path = generateUniqueRenamedPath(fs_.get(), item->dst_path);
            item->progress.destination_path = item->dst_path;
        }

        if (dstStat.is_read_only) {
            fs_->setPermissions(item->dst_path, dstStat.mode | S_IWUSR);
        }
    }

    int srcFd = fs_->openFile(item->src_path, O_RDONLY);
    if (srcFd < 0) return false;

    int dstFd = fs_->openFile(item->dst_path, O_WRONLY | O_CREAT | O_TRUNC, srcStat.mode ? srcStat.mode : 0644);
    if (dstFd < 0) {
        fs_->closeFile(srcFd);
        return false;
    }

    bool success = false;
    if (options_.enable_cow && tryCoWTransfer(item, srcFd, dstFd)) {
        success = true;
    } else if (srcStat.is_sparse && options_.enable_sparse && transferSparseFile(item, srcFd, dstFd)) {
        success = true;
    } else if (srcStat.size <= static_cast<off_t>(options_.small_file_mmap_threshold) && transferSmallFileMmap(item, srcFd, dstFd)) {
        success = true;
    } else {
        success = transferChunked(item, srcFd, dstFd);
    }

    fs_->closeFile(srcFd);
    fs_->closeFile(dstFd);

    if (success) {
        fs_->copyMetadata(item->src_path, item->dst_path, options_.preserve_ownership);
        if (options_.mode == TransferMode::MOVE || (job && job->mode == TransferMode::MOVE)) {
            fs_->removeFile(item->src_path);
        }
    }
    return success;
}

bool TransferManager::tryCoWTransfer(std::shared_ptr<TransferItem> item, int srcFd, int dstFd) {
    if (fs_->tryCloneOrCoW(srcFd, dstFd, item->size)) {
        cow_bytes_.fetch_add(item->size, std::memory_order_relaxed);
        item->progress.bytes_transferred = item->size;
        item->progress.progress_percent = 100.0;
        return true;
    }
    return false;
}

bool TransferManager::transferSparseFile(std::shared_ptr<TransferItem> item, int srcFd, int dstFd) {
    off_t offset = 0;
    while (offset < item->size) {
        off_t dataStart = fs_->seekData(srcFd, offset);
        if (dataStart < 0) {
            fs_->truncateFile(dstFd, item->size);
            break;
        }

        off_t holeStart = fs_->seekHole(srcFd, dataStart);
        if (holeStart < 0) holeStart = item->size;

        if (dataStart > offset) {
            sparse_bytes_skipped_.fetch_add(dataStart - offset, std::memory_order_relaxed);
        }

        size_t bytesToCopy = holeStart - dataStart;
        std::vector<char> buf(std::min<size_t>(bytesToCopy, 1024 * 1024));
        off_t curr = dataStart;
        while (curr < holeStart) {
            size_t chunk = std::min<size_t>(buf.size(), holeStart - curr);
            ssize_t r = fs_->readFile(srcFd, buf.data(), chunk, curr);
            if (r <= 0) break;
            fs_->writeFile(dstFd, buf.data(), r, curr);
            curr += r;
            item->progress.bytes_transferred += r;
        }
        offset = holeStart;
    }
    fs_->truncateFile(dstFd, item->size);
    return true;
}

bool TransferManager::transferSmallFileMmap(std::shared_ptr<TransferItem> item, int srcFd, int dstFd) {
    if (item->size == 0) return true;
    void* srcMap = fs_->mapFile(srcFd, item->size, 0, true);
    if (!srcMap) return false;

    fs_->truncateFile(dstFd, item->size);
    void* dstMap = fs_->mapFile(dstFd, item->size, 0, false);
    if (!dstMap) {
        fs_->unmapFile(srcMap, item->size);
        return false;
    }

    std::memcpy(dstMap, srcMap, item->size);
    fs_->unmapFile(srcMap, item->size);
    fs_->unmapFile(dstMap, item->size);

    mmap_bytes_.fetch_add(item->size, std::memory_order_relaxed);
    item->progress.bytes_transferred = item->size;
    item->progress.progress_percent = 100.0;
    return true;
}

bool TransferManager::transferChunked(std::shared_ptr<TransferItem> item, int srcFd, int dstFd) {
    size_t chunkSize = options_.chunk_size;
    off_t offset = 0;

    std::shared_ptr<TransferJobRequest> job;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = jobs_.find(item->job_id);
        if (it != jobs_.end()) job = it->second;
    }

    while (offset < item->size && service_running_.load(std::memory_order_acquire)) {
        if (job && (job->is_cancelled || job->is_paused)) break;
        if (!buffer_ring_ || buffer_ring_->isShutdown()) break;

        BufferChunk* chunk = buffer_ring_->acquireFreeChunk();
        if (!chunk) break;

        size_t toRead = std::min<size_t>(chunkSize, item->size - offset);
        ssize_t r = fs_->readFile(srcFd, chunk->buffer, toRead, offset);
        if (r < 0) {
            buffer_ring_->markChunkFailed(chunk, errno);
            return false;
        }

        chunk->src_fd = srcFd;
        chunk->dst_fd = dstFd;
        chunk->src_offset = offset;
        chunk->dst_offset = offset;
        chunk->bytes_valid = r;
        chunk->is_eof = (offset + r >= item->size);

        buffer_ring_->publishReadyChunk(chunk, r, chunk->is_eof);

        offset += r;
        item->progress.bytes_transferred = offset;
        item->progress.progress_percent = (item->size > 0) ? (offset * 100.0 / item->size) : 100.0;

        if (job) {
            job->progress.bytes_transferred += r;
            if (job->progress.total_bytes > 0) {
                job->progress.progress_percent = (job->progress.bytes_transferred * 100.0) / job->progress.total_bytes;
            }
            pitchItemProgress(job, item);
        }
    }
    return (offset >= item->size);
}

void TransferManager::pitchItemProgress(std::shared_ptr<TransferJobRequest> job, std::shared_ptr<TransferItem> item, bool forceFinal) {
    if (!job || !item) return;

    auto now = std::chrono::steady_clock::now();
    if (!forceFinal && job->last_pitch_time.time_since_epoch().count() != 0) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - job->last_pitch_time).count();
        if (elapsedMs < 50) {
            return;
        }
        if (elapsedMs > 0) {
            uint64_t delta = job->progress.bytes_transferred - job->last_pitch_bytes;
            job->progress.current_speed_mb_s = (delta / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
        }
    }

    job->last_pitch_time = now;
    job->last_pitch_bytes = job->progress.bytes_transferred;

    onTransferProgress(item->progress, job->progress);
}

void TransferManager::finishItem(std::shared_ptr<TransferJobRequest> job, std::shared_ptr<TransferItem> item) {
    if (!job || !item) return;

    item->progress.is_completed = true;
    item->progress.progress_percent = 100.0;

    if (item->is_directory) {
        job->progress.dirs_transferred++;
    } else {
        job->progress.files_transferred++;
    }

    onTransferProgress(item->progress, job->progress);

    // Prune completed item immediately from active_items
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        auto it = std::find(job->active_items.begin(), job->active_items.end(), item);
        if (it != job->active_items.end()) {
            job->active_items.erase(it);
        }
    }

    checkJobCompletion(job);
}

void TransferManager::checkJobCompletion(std::shared_ptr<TransferJobRequest> job) {
    if (!job) return;

    bool done = false;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (job->is_scan_completed && job->active_items.empty() && !job->is_completed) {
            job->is_completed = true;
            job->progress.is_completed = true;
            job->progress.progress_percent = 100.0;
            done = true;
        }
    }

    if (done) {
        TransferItemProgress finalItem;
        finalItem.source_path = job->source_path;
        finalItem.destination_path = job->destination_dir;
        finalItem.is_completed = true;
        finalItem.progress_percent = 100.0;

        onTransferProgress(finalItem, job->progress);

        cv_job_done_.notify_all();
        cv_all_done_.notify_all();
    }
}

// --- Callbacks and Dispatchers ---

void TransferManager::onTransferProgress(const TransferItemProgress& currentItem, const TransferProgress& jobProgress) {
    if (item_transfer_progress_cb_) {
        item_transfer_progress_cb_(currentItem, jobProgress);
    }
}

void TransferManager::onFolderScanProgress(const FolderScanProgress& scan) {
    if (scan_progress_cb_) {
        scan_progress_cb_(scan);
    }
}

QuestionAnswer TransferManager::onQuestion(const TransferQuestion& question, bool& rememberForType) {
    if (question_cb_) {
        return question_cb_(question, rememberForType);
    }
    return QuestionAnswer::OVERWRITE;
}
