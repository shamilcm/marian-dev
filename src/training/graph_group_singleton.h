#pragma once

#include "training/graph_group.h"
#include "common/filesystem.h"

#include <future>

namespace marian {

/**
 * Single GPU training
 */
class SingletonGraph : public GraphGroup {
public:
  virtual void setScheduler(Ptr<Scheduler> scheduler) override;

private:
  void execute(Ptr<data::Batch> batch);
  void barrier() const override {} // dummy barrier

public:
  SingletonGraph(Ptr<Options> options, Ptr<IMPIWrapper> mpi)
      : GraphGroup(options) {
    ABORT_IF(mpi->numMPIProcesses() != 1, "SingletonGraph does not support multiple MPI processes");
    ABORT_IF(devices_.size() != 1, "Only one device ID should be provided for singleton training");
    auto deviceId = devices_[0];
    // Initialize graph

    graphs_.push_back(New<ExpressionGraph>());

    auto graph = graphs_[0]; // only one graph
    graph->setDevice(deviceId);

    auto precisions = options_->get<std::vector<std::string>>("precision");
    graph->setParameterType(typeFromString(precisions[0]));

    if(options_->get<bool>("check-nan"))
      graph->setThrowNan(true);

    graph->getBackend()->setClip(options_->get<float>("clip-gemm"));
    graph->reserveWorkspaceMB(options_->get<size_t>("workspace"));

    optimizerShards_.push_back(Optimizer(options_));
    models_.push_back(models::from_options(options_, models::usage::training));
  }

  void update(Ptr<data::Batch> batch) override {
    validate();
    execute(batch);
  }

  void load() override {
    auto scatterFn = [&](const io::Item& optimizerState, const OptimizerBase::ScatterStateSetFunc& setFn) {
      setFn(/*localDeviceIndex=*/0, optimizerState.bytes.data(), optimizerState.bytes.data() + optimizerState.size());
    };

    // This function loads the main parameters in the graphs.
    GraphGroup::load(scatterFn);
  }

  void save(bool isFinal = false) override {
    auto distParams = [](){}; // do nothing, only one process and GPU

    auto gatherOpt  = [&](const OptimizerBase::GatherStateGetFunc& getFn) {
      return getFn(/*localDeviceIndex=*/0); // dummy
    };

    GraphGroup::save(isFinal, distParams, gatherOpt, /*isMainProcess=*/true);
  }

  Ptr<data::BatchStats> collectStats(const std::vector<Ptr<Vocab>>& vocabs) {
    return GraphGroup::collectStats(graphs_[0], models_[0], vocabs);
  }

  virtual void finalize() override { finalized_ = true; }
};
}  // namespace marian
