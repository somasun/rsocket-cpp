// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/Memory.h>
#include <folly/io/IOBuf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ConnectionAutomaton.h"
#include "src/Frame.h"
#include "src/FrameTransport.h"
#include "src/ResumeCache.h"
#include "src/Stats.h"
#include "src/versions/FrameSerializer_v0_1.h"
#include "test/InlineConnection.h"
#include "test/MockStats.h"

using namespace ::testing;
using namespace ::reactivesocket;

class FrameTransportMock : public FrameTransport {
 public:
  FrameTransportMock() : FrameTransport(std::make_unique<InlineConnection>()) {}

  MOCK_METHOD1(outputFrameOrEnqueue_, void(std::unique_ptr<folly::IOBuf>&));

  void outputFrameOrEnqueue(std::unique_ptr<folly::IOBuf> frame) override {
    outputFrameOrEnqueue_(frame);
  }
};

class ResumeCacheTest : public Test {
 protected:
  std::unique_ptr<FrameSerializer> frameSerializer_{
      FrameSerializer::createCurrentVersion()};
};

TEST_F(ResumeCacheTest, EmptyCache) {
  ConnectionAutomaton automaton(
      inlineExecutor(),
      nullptr,
      nullptr,
      nullptr,
      Stats::noop(),
      nullptr,
      ReactiveSocketMode::CLIENT);
  automaton.setFrameSerializer(FrameSerializer::createCurrentVersion());
  ResumeCache cache(automaton);
  FrameTransportMock transport;

  EXPECT_CALL(transport, outputFrameOrEnqueue_(_)).Times(0);

  EXPECT_EQ(0, cache.lastResetPosition());
  EXPECT_EQ(0, cache.position());
  EXPECT_TRUE(cache.isPositionAvailable(0));
  EXPECT_FALSE(cache.isPositionAvailable(1));
  cache.sendFramesFromPosition(0, transport);

  cache.resetUpToPosition(0);

  EXPECT_EQ(0, cache.lastResetPosition());
  EXPECT_EQ(0, cache.position());
  EXPECT_TRUE(cache.isPositionAvailable(0));
  EXPECT_FALSE(cache.isPositionAvailable(1));
  cache.sendFramesFromPosition(0, transport);
}

TEST_F(ResumeCacheTest, OneFrame) {
  ConnectionAutomaton automaton(
      inlineExecutor(),
      nullptr,
      nullptr,
      nullptr,
      Stats::noop(),
      nullptr,
      ReactiveSocketMode::CLIENT);
  automaton.setFrameSerializer(FrameSerializer::createCurrentVersion());
  ResumeCache cache(automaton);

  FrameTransportMock transport;

  auto frame1 = frameSerializer_->serializeOut(Frame_CANCEL(0));
  const auto frame1Size = frame1->computeChainDataLength();

  cache.trackSentFrame(*frame1);

  EXPECT_EQ(0, cache.lastResetPosition());
  EXPECT_EQ((ResumePosition)frame1Size, cache.position());
  EXPECT_TRUE(cache.isPositionAvailable(0));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size));

  cache.resetUpToPosition(0);

  EXPECT_EQ(0, cache.lastResetPosition());
  EXPECT_EQ((ResumePosition)frame1Size, cache.position());
  EXPECT_TRUE(cache.isPositionAvailable(0));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size));

  EXPECT_FALSE(cache.isPositionAvailable(frame1Size - 1)); // misaligned

  EXPECT_CALL(transport, outputFrameOrEnqueue_(_))
      .WillOnce(Invoke([=](std::unique_ptr<folly::IOBuf>& buf) {
        EXPECT_EQ(frame1Size, buf->computeChainDataLength());
      }));

  cache.sendFramesFromPosition(0, transport);
  cache.sendFramesFromPosition(frame1Size, transport);

  cache.resetUpToPosition(frame1Size);

  EXPECT_EQ((ResumePosition)frame1Size, cache.lastResetPosition());
  EXPECT_EQ((ResumePosition)frame1Size, cache.position());
  EXPECT_FALSE(cache.isPositionAvailable(0));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size));

  cache.sendFramesFromPosition(frame1Size, transport);
}

TEST_F(ResumeCacheTest, TwoFrames) {
  ConnectionAutomaton automaton(
      inlineExecutor(),
      nullptr,
      nullptr,
      nullptr,
      Stats::noop(),
      nullptr,
      ReactiveSocketMode::CLIENT);
  automaton.setFrameSerializer(FrameSerializer::createCurrentVersion());
  ResumeCache cache(automaton);

  FrameTransportMock transport;

  auto frame1 = frameSerializer_->serializeOut(Frame_CANCEL(0));
  const auto frame1Size = frame1->computeChainDataLength();

  auto frame2 = frameSerializer_->serializeOut(Frame_REQUEST_N(0, 0));
  const auto frame2Size = frame2->computeChainDataLength();

  cache.trackSentFrame(*frame1);
  cache.trackSentFrame(*frame2);

  EXPECT_EQ(0, cache.lastResetPosition());
  EXPECT_EQ((ResumePosition)(frame1Size + frame2Size), cache.position());
  EXPECT_TRUE(cache.isPositionAvailable(0));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size + frame2Size));

  EXPECT_CALL(transport, outputFrameOrEnqueue_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& buf) {
        EXPECT_EQ(frame1Size, buf->computeChainDataLength());
      }))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& buf) {
        EXPECT_EQ(frame2Size, buf->computeChainDataLength());
      }));

  cache.sendFramesFromPosition(0, transport);

  cache.resetUpToPosition(frame1Size);

  EXPECT_EQ((ResumePosition)frame1Size, cache.lastResetPosition());
  EXPECT_EQ((ResumePosition)(frame1Size + frame2Size), cache.position());
  EXPECT_FALSE(cache.isPositionAvailable(0));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size));
  EXPECT_TRUE(cache.isPositionAvailable(frame1Size + frame2Size));

  EXPECT_CALL(transport, outputFrameOrEnqueue_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& buf) {
        EXPECT_EQ(frame2Size, buf->computeChainDataLength());
      }));

  cache.sendFramesFromPosition(frame1Size, transport);
}

TEST_F(ResumeCacheTest, Stats) {
  StrictMock<MockStats> stats;
  ConnectionAutomaton automaton(
      inlineExecutor(),
      nullptr,
      nullptr,
      nullptr,
      stats,
      nullptr,
      ReactiveSocketMode::CLIENT);
  automaton.setFrameSerializer(FrameSerializer::createCurrentVersion());
  ResumeCache cache(automaton);

  auto frame1 = frameSerializer_->serializeOut(Frame_CANCEL(0));
  auto frame1Size = frame1->computeChainDataLength();
  EXPECT_CALL(stats, resumeBufferChanged(1, frame1Size));
  cache.trackSentFrame(*frame1);

  auto frame2 = frameSerializer_->serializeOut(Frame_REQUEST_N(0, 0));
  auto frame2Size = frame2->computeChainDataLength();
  EXPECT_CALL(stats, resumeBufferChanged(1, frame2Size)).Times(2);
  cache.trackSentFrame(*frame2);
  cache.trackSentFrame(*frame2);

  EXPECT_CALL(stats, resumeBufferChanged(-1, -frame1Size));
  cache.resetUpToPosition(frame1Size);
  EXPECT_CALL(stats, resumeBufferChanged(-2, -2 * frame2Size));
}
