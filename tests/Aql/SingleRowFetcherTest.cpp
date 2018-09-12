////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Tobias Gödderz
/// @author Michael Hackstein
/// @author Heiko Kernbach
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#include "AqlItemBlockHelper.h"
#include "BlockFetcherHelper.h"
#include "catch.hpp"
#include "fakeit.hpp"

#include "Aql/AqlItemBlock.h"
#include "Aql/AqlItemRow.h"
#include "Aql/BlockFetcher.h"
#include "Aql/ExecutionBlock.h"
#include "Aql/ExecutorInfos.h"
#include "Aql/FilterExecutor.h"
#include "Aql/ResourceUsage.h"
#include "Aql/SingleRowFetcher.h"

#include <velocypack/Builder.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;
using namespace arangodb::aql;

namespace arangodb {
namespace tests {
namespace aql {

SCENARIO("SingleRowFetcher", "[AQL][EXECUTOR]") {
  ExecutionState state;
  AqlItemRow const* row;

  GIVEN("there are no blocks upstream") {
    VPackBuilder input;
    fakeit::Mock<BlockFetcher> blockFetcherMock;

    WHEN("the producer does not wait") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock)).Do([] {
        return std::make_pair(ExecutionState::DONE, nullptr);
      });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should return DONE with nullptr") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::DONE);
        REQUIRE(row == nullptr);
        fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Once();
        fakeit::VerifyNoOtherInvocations(blockFetcherMock);
      }
    }

    WHEN("the producer waits") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock))
          .Do([] { return std::make_pair(ExecutionState::WAITING, nullptr); })
          .Do([] { return std::make_pair(ExecutionState::DONE, nullptr); });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should first return WAIT with nullptr") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::WAITING);
        REQUIRE(row == nullptr);

        AND_THEN("the fetcher should return DONE with nullptr") {
          std::tie(state, row) = testee.fetchRow();
          REQUIRE(state == ExecutionState::DONE);
          REQUIRE(row == nullptr);
          fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Twice();
          fakeit::VerifyNoOtherInvocations(blockFetcherMock);
        }
      }
    }
  }

  GIVEN("A single upstream block with a single row") {
    VPackBuilder input;
    fakeit::Mock<BlockFetcher> blockFetcherMock;

    ResourceMonitor monitor;
    std::unique_ptr<AqlItemBlock> block =
        std::make_unique<AqlItemBlock>(&monitor, 1, 1);
    // using an int asserts that the AqlValueType is INLINE, so we can just use
    // slice() later
    AqlValue const val{AqlValueHintInt{42}};
    block->setValue(0, 0, val);

    WHEN("the producer returns DONE immediately") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock)).Do([&block] {
        return std::make_pair(ExecutionState::DONE, std::move(block));
      });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should return the row with DONE") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::DONE);
        REQUIRE(row != nullptr);
        REQUIRE(row->getNrRegisters() == 1);
        REQUIRE(row->getValue(0).slice().getInt() == 42);
        fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Once();
        fakeit::VerifyNoOtherInvocations(blockFetcherMock);
      }
    }

    WHEN("the producer returns HASMORE, then DONE with a nullptr") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock))
          .Do([&block] {
            return std::make_pair(ExecutionState::HASMORE, std::move(block));
          })
          .Do([] { return std::make_pair(ExecutionState::DONE, nullptr); });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should return the row with HASMORE") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::HASMORE);
        REQUIRE(row != nullptr);
        REQUIRE(row->getNrRegisters() == 1);
        REQUIRE(row->getValue(0).slice().getInt() == 42);

        AND_THEN("the fetcher shall return DONE") {
          std::tie(state, row) = testee.fetchRow();
          REQUIRE(state == ExecutionState::DONE);
          REQUIRE(row == nullptr);

          fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Twice();
          fakeit::VerifyNoOtherInvocations(blockFetcherMock);
        }
      }
    }

    WHEN("the producer WAITs, then returns DONE") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock))
          .Do([] { return std::make_pair(ExecutionState::WAITING, nullptr); })
          .Do([&block] {
            return std::make_pair(ExecutionState::DONE, std::move(block));
          });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should first return WAIT with nullptr") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::WAITING);
        REQUIRE(row == nullptr);

        AND_THEN("the fetcher should return the row with DONE") {
          std::tie(state, row) = testee.fetchRow();
          REQUIRE(state == ExecutionState::DONE);
          REQUIRE(row != nullptr);
          REQUIRE(row->getNrRegisters() == 1);
          REQUIRE(row->getValue(0).slice().getInt() == 42);
          fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Twice();
          fakeit::VerifyNoOtherInvocations(blockFetcherMock);
        }
      }
    }

    WHEN("the producer WAITs, returns HASMORE, then DONE") {
      // Using .Return doesn't work here, as unique_ptr is not
      // copy-constructible.
      fakeit::When(Method(blockFetcherMock, fetchBlock))
          .Do([] { return std::make_pair(ExecutionState::WAITING, nullptr); })
          .Do([&block] {
            return std::make_pair(ExecutionState::HASMORE, std::move(block));
          })
          .Do([] { return std::make_pair(ExecutionState::DONE, nullptr); });

      SingleRowFetcher testee(blockFetcherMock.get());

      THEN("the fetcher should first return WAIT with nullptr") {
        std::tie(state, row) = testee.fetchRow();
        REQUIRE(state == ExecutionState::WAITING);
        REQUIRE(row == nullptr);

        AND_THEN("the fetcher should return the row with HASMORE") {
          std::tie(state, row) = testee.fetchRow();
          REQUIRE(state == ExecutionState::HASMORE);
          REQUIRE(row != nullptr);
          REQUIRE(row->getNrRegisters() == 1);
          REQUIRE(row->getValue(0).slice().getInt() == 42);

          AND_THEN("the fetcher shall return DONE") {
            std::tie(state, row) = testee.fetchRow();
            REQUIRE(state == ExecutionState::DONE);
            REQUIRE(row == nullptr);

            fakeit::Verify(Method(blockFetcherMock, fetchBlock)).Exactly(3);
            fakeit::VerifyNoOtherInvocations(blockFetcherMock);
          }
        }
      }
    }
  }

  GIVEN("there are multiple blocks upstream") {
    fakeit::Mock<BlockFetcher> blockFetcherMock;

    // a 1-column matrix with 3 rows
    std::unique_ptr<AqlItemBlock> block1 = buildBlock<2>({{1}, {2}, {3}}),
                                  block2 = buildBlock<2>({{4}, {5}, {6}}),
                                  block3 = buildBlock<2>({{7}, {8}, {9}});

    // TODO implement some tests for this case

    WHEN("the producer does not wait") {}
  }
}

}  // namespace aql
}  // namespace tests
}  // namespace arangodb
