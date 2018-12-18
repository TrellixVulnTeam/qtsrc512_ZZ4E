// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ASSIST_RANKER_RANKER_MODEL_LOADER_IMPL_H_
#define COMPONENTS_ASSIST_RANKER_RANKER_MODEL_LOADER_IMPL_H_

#include "components/assist_ranker/ranker_model_loader.h"

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace network {
class SharedURLLoaderFactory;
}

namespace assist_ranker {

class RankerURLFetcher;

// Loads a ranker model. Will attempt to load the model from disk cache. If it
// fails, will attempt to download from the given URL.
class RankerModelLoaderImpl : public RankerModelLoader {
 public:
  // |validate_model_callback| may be called on any sequence; it must be thread
  // safe.
  //
  // |on_model_available_callback| will be called on the sequence on which the
  // ranker model loader is constructed.
  //
  // |model_path| denotes the file path at which the model is cached. The loader
  // will attempt to load the model from this path first, falling back to the
  // |model_url| if the model cannot be loaded or has expired. Upon downloading
  // a fresh model from |model_url| the model will be persisted to |model_path|
  // for subsequent caching.
  //
  // |model_url| denotes the URL from which the model should be loaded, if it
  // has not already been cached at |model_path|.
  //
  // |uma_prefix| will be used as a prefix for the names of all UMA metrics
  // generated by this loader.
  RankerModelLoaderImpl(
      ValidateModelCallback validate_model_callback,
      OnModelAvailableCallback on_model_available_callback,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::FilePath model_path,
      GURL model_url,
      std::string uma_prefix);

  ~RankerModelLoaderImpl() override;

  // Call this method periodically to notify the model loader the ranker is
  // actively in use. The user's engagement with the ranked feature is used
  // as a proxy for network availability and activity. If a model download
  // is pending, this will trigger (subject to retry and frequency limits) a
  // model download attempt.
  void NotifyOfRankerActivity() override;

 private:
  // A enum to track the current loader state.
  enum class LoaderState {
    // The loader is newly created and has not started trying to load the model.
    // This state can transition to LOADING_FROM_FILE or, if |model_path_| is
    // empty, to LOADING_FROM_URL. If both |model_path_| and |model_url_| are
    // empty/invalid then it can transition to FINISHED.
    NOT_STARTED,
    // The loader is busy loading the model from |model_path_| in the
    // background. This state can transition to FINISHED if the loaded model is
    // compatible and up to date; otherwise, this state can transition to IDLE.
    LOADING_FROM_FILE,
    // The loader is not currently busy. The loader can transition to the
    // LOADING_FROM_URL_ state if |model_url_| is valid; the loader can also
    // transition to FINISHED if it the maximum number of download attempts
    // has been reached.
    IDLE,
    // The loader is busy loading the model from |model_url_| in the background.
    // This state can transition to FINISHED if the loaded model is valid;
    // otherwise, this state can re-transition to IDLE.
    LOADING_FROM_URL,
    // The loader has finished. This is the terminal state.
    FINISHED
  };

  // Asynchronously initiates loading the model from model_path_;
  void StartLoadFromFile();

  // Called when the background worker has finished loading |data| from
  // |model_path_|. If |data| is empty, the load from |model_path_| failed.
  void OnFileLoaded(const std::string& data);

  // Asynchronously initiates loading the model from |model_url_|.
  void StartLoadFromURL();

  // Called when |url_fetcher_| has finished loading |data| from |model_url_|.
  //
  // This call signature is mandated by RankerURLFetcher.
  //
  // success - true of the download was successful
  // data - the body of the downloads response
  void OnURLFetched(bool success, const std::string& data);

  // Parse |data| and return a validated model. Returns nullptr on failure.
  std::unique_ptr<assist_ranker::RankerModel> CreateAndValidateModel(
      const std::string& data);

  // Helper function to log |model_status| to UMA and return it.
  RankerModelStatus ReportModelStatus(RankerModelStatus model_status);

  // Validates that ranker model loader tasks are all performed on the same
  // sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // The task runner on which background tasks are performed.
  const scoped_refptr<base::SequencedTaskRunner> background_task_runner_;

  // Validates a ranker model on behalf of the model loader client. This will be
  // called on the sequence on which the model leader was constructed.
  const ValidateModelCallback validate_model_cb_;

  // Transfers ownership of a loaded model back to the model loader client.
  // This will be called on the sequence on which the model loader was
  // constructed.
  const OnModelAvailableCallback on_model_available_cb_;

  // URL loader factory used for RankerURLFetcher.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // The path at which the model is (or should be) cached.
  const base::FilePath model_path_;

  // The URL from which to download the model if the model is not in the cache
  // or the cached model is invalid/expired.
  const GURL model_url_;

  // This will prefix all UMA metrics generated by the model loader.
  const std::string uma_prefix_;

  // Used to download model data from |model_url_|.
  std::unique_ptr<RankerURLFetcher> url_fetcher_;

  // The next time before which no new attempts to download the model should be
  // attempted.
  base::TimeTicks next_earliest_download_time_;

  // Tracks the last time of the last attempt to load a model, either from file
  // of from URL. Used for UMA reporting of load durations.
  base::TimeTicks load_start_time_;

  // The current state of the loader.
  LoaderState state_ = LoaderState::NOT_STARTED;

  // Creates weak pointer references to the loader.
  base::WeakPtrFactory<RankerModelLoaderImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(RankerModelLoaderImpl);
};

}  // namespace assist_ranker

#endif  // COMPONENTS_ASSIST_RANKER_RANKER_MODEL_LOADER_IMPL_H_