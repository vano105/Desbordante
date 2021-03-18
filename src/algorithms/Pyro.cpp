#include <chrono>

#include "FdG1Strategy.h"
#include "KeyG1Strategy.h"
#include "Pyro.h"

unsigned long long Pyro::execute() {
    auto startTime = std::chrono::system_clock::now();

    auto relation = ColumnLayoutRelationData::createFrom(inputGenerator_, configuration_.isNullEqualNull);
    auto schema = relation->getSchema();
    if (relation->getColumnData().empty()) {
        throw std::runtime_error("Got an empty .csv file: FD mining is meaningless.");
    }

    auto profilingContext = std::make_unique<ProfilingContext>(
            configuration_,
            relation.get(),
            uccConsumer_,
            fdConsumer_,
            cachingMethod_,
            evictionMethod_,
            cachingMethodValue
            );

    std::function<bool(DependencyCandidate const&, DependencyCandidate const&)> launchPadOrder;
    if (configuration_.launchPadOrder == "arity") {
        launchPadOrder = DependencyCandidate::fullArityErrorComparator;
    } else if (configuration_.launchPadOrder == "error") {
        launchPadOrder = DependencyCandidate::fullErrorArityComparator;
    } else {
        throw std::runtime_error("Unknown comparator type");
    }

    int nextId = 0;
    if (configuration_.isFindKeys) {
        std::unique_ptr<DependencyStrategy> strategy;
        if (configuration_.uccErrorMeasure == "g1prime") {
            strategy = std::make_unique<KeyG1Strategy>(configuration_.maxUccError, configuration_.errorDev);
        } else {
            throw std::runtime_error("Unknown key error measure.");
        }
        searchSpaces_.push_back(std::make_unique<SearchSpace>(nextId++, std::move(strategy), schema, launchPadOrder));
    }
    if (configuration_.isFindFds) {
        for (auto& rhs : schema->getColumns()) {
            std::unique_ptr<DependencyStrategy> strategy;
            if (configuration_.uccErrorMeasure == "g1prime") {
                strategy = std::make_unique<FdG1Strategy>(rhs.get(), configuration_.maxUccError, configuration_.errorDev);
            } else {
                throw std::runtime_error("Unknown key error measure.");
            }
            searchSpaces_.push_back(std::make_unique<SearchSpace>(nextId++, std::move(strategy), schema, launchPadOrder));
        }
    }
    unsigned long long initTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime).count();

    startTime = std::chrono::system_clock::now();
    unsigned int totalErrorCalcCount = 0;
    unsigned long long totalAscension = 0;
    unsigned long long totalTrickle = 0;
    for (auto& searchSpace : searchSpaces_) {
        searchSpace->setContext(profilingContext.get());
        searchSpace->ensureInitialized();
        searchSpace->discover();
        // searchSpace->printStats();
        totalErrorCalcCount += searchSpace->getErrorCalcCount();
        totalAscension += searchSpace->ascending / 1000000;
        totalTrickle += searchSpace->tricklingDown / 1000000;
    }
    auto elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime);

    LOG(DEBUG) << boost::format{"FdG1 error calculation: %1% ms"} % (FdG1Strategy::nanos_ / 1000000);
    std::cout << "Init time: " << initTimeMillis << "ms" << std::endl;
    std::cout << "Time: " << elapsed_milliseconds.count() << " milliseconds" << std::endl;
    std::cout << "Error calculation count: " << totalErrorCalcCount << std::endl;
    std::cout << "Total ascension time: " << totalAscension << "ms" << std::endl;
    std::cout << "Total trickle time: " << totalTrickle << "ms" << std::endl;
    std::cout << "Total intersection time: " << PositionListIndex::micros / 1000 << "ms" << std::endl;
    /*std::cout << "====RESULTS-FD====\r\n" << fdsToString();
    std::cout << "====RESULTS-UCC====\r\n" << uccsToString();
    std::cout << "====JSON-FD========\r\n" << FDAlgorithm::getJsonFDs() << std::endl;*/
    std::cout << "HASH: " << FDAlgorithm::fletcher16() << std::endl;
    return elapsed_milliseconds.count();
}


Pyro::Pyro(std::filesystem::path const &path, char separator, bool hasHeader, int seed, double maxError, unsigned int maxLHS) :
        FDAlgorithm(path, separator, hasHeader),
        cachingMethod_(CachingMethod::COIN),
        evictionMethod_(CacheEvictionMethod::DEFAULT) {
    uccConsumer_ = [this](auto const& key) { this->discoveredUCCs_.push_back(key); };
    fdConsumer_ = [this](auto const& fd) {
        this->discoveredFDs_.push_back(fd); this->fdCollection_.emplace_back(*fd.lhs_, *fd.rhs_); };
    configuration_.seed = seed;
    configuration_.maxUccError = maxError;
    configuration_.maxUccError = maxError;
    configuration_.maxLHS = maxLHS;
}