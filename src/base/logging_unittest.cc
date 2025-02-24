// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_POSIX)
#include <signal.h>
#include <unistd.h>
#include "base/posix/eintr_wrapper.h"
#endif  // OS_POSIX

#if defined(OS_LINUX) || defined(OS_ANDROID)
#include <ucontext.h>
#endif

#if defined(OS_WIN)
#include <excpt.h>
#include <windows.h>
#endif  // OS_WIN

#if defined(OS_FUCHSIA)
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/process.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include "base/fuchsia/fuchsia_logging.h"
#endif

namespace logging {

namespace {

using ::testing::Return;
using ::testing::_;

// Needs to be global since log assert handlers can't maintain state.
int g_log_sink_call_count = 0;

#if !defined(OFFICIAL_BUILD) || defined(DCHECK_ALWAYS_ON) || !defined(NDEBUG)
void LogSink(const char* file,
             int line,
             const base::StringPiece message,
             const base::StringPiece stack_trace) {
  ++g_log_sink_call_count;
}
#endif

// Class to make sure any manipulations we do to the min log level are
// contained (i.e., do not affect other unit tests).
class LogStateSaver {
 public:
  LogStateSaver() : old_min_log_level_(GetMinLogLevel()) {}

  ~LogStateSaver() {
    SetMinLogLevel(old_min_log_level_);
    g_log_sink_call_count = 0;
  }

 private:
  int old_min_log_level_;

  DISALLOW_COPY_AND_ASSIGN(LogStateSaver);
};

class LoggingTest : public testing::Test {
 private:
  LogStateSaver log_state_saver_;
};

class MockLogSource {
 public:
  MOCK_METHOD0(Log, const char*());
};

class MockLogAssertHandler {
 public:
  MOCK_METHOD4(
      HandleLogAssert,
      void(const char*, int, const base::StringPiece, const base::StringPiece));
};

TEST_F(LoggingTest, BasicLogging) {
  MockLogSource mock_log_source;
  EXPECT_CALL(mock_log_source, Log())
      .Times(DCHECK_IS_ON() ? 16 : 8)
      .WillRepeatedly(Return("log message"));

  SetMinLogLevel(LOG_INFO);

  EXPECT_TRUE(LOG_IS_ON(INFO));
  EXPECT_TRUE((DCHECK_IS_ON() != 0) == DLOG_IS_ON(INFO));
  EXPECT_TRUE(VLOG_IS_ON(0));

  LOG(INFO) << mock_log_source.Log();
  LOG_IF(INFO, true) << mock_log_source.Log();
  PLOG(INFO) << mock_log_source.Log();
  PLOG_IF(INFO, true) << mock_log_source.Log();
  VLOG(0) << mock_log_source.Log();
  VLOG_IF(0, true) << mock_log_source.Log();
  VPLOG(0) << mock_log_source.Log();
  VPLOG_IF(0, true) << mock_log_source.Log();

  DLOG(INFO) << mock_log_source.Log();
  DLOG_IF(INFO, true) << mock_log_source.Log();
  DPLOG(INFO) << mock_log_source.Log();
  DPLOG_IF(INFO, true) << mock_log_source.Log();
  DVLOG(0) << mock_log_source.Log();
  DVLOG_IF(0, true) << mock_log_source.Log();
  DVPLOG(0) << mock_log_source.Log();
  DVPLOG_IF(0, true) << mock_log_source.Log();
}

TEST_F(LoggingTest, LogIsOn) {
#if defined(NDEBUG)
  const bool kDfatalIsFatal = false;
#else  // defined(NDEBUG)
  const bool kDfatalIsFatal = true;
#endif  // defined(NDEBUG)

  SetMinLogLevel(LOG_INFO);
  EXPECT_TRUE(LOG_IS_ON(INFO));
  EXPECT_TRUE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  SetMinLogLevel(LOG_WARNING);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_TRUE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  SetMinLogLevel(LOG_ERROR);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_FALSE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  // LOG_IS_ON(FATAL) should always be true.
  SetMinLogLevel(LOG_FATAL + 1);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_FALSE(LOG_IS_ON(WARNING));
  EXPECT_FALSE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_EQ(kDfatalIsFatal, LOG_IS_ON(DFATAL));
}

TEST_F(LoggingTest, LoggingIsLazyBySeverity) {
  MockLogSource mock_log_source;
  EXPECT_CALL(mock_log_source, Log()).Times(0);

  SetMinLogLevel(LOG_WARNING);

  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_FALSE(DLOG_IS_ON(INFO));
  EXPECT_FALSE(VLOG_IS_ON(1));

  LOG(INFO) << mock_log_source.Log();
  LOG_IF(INFO, false) << mock_log_source.Log();
  PLOG(INFO) << mock_log_source.Log();
  PLOG_IF(INFO, false) << mock_log_source.Log();
  VLOG(1) << mock_log_source.Log();
  VLOG_IF(1, true) << mock_log_source.Log();
  VPLOG(1) << mock_log_source.Log();
  VPLOG_IF(1, true) << mock_log_source.Log();

  DLOG(INFO) << mock_log_source.Log();
  DLOG_IF(INFO, true) << mock_log_source.Log();
  DPLOG(INFO) << mock_log_source.Log();
  DPLOG_IF(INFO, true) << mock_log_source.Log();
  DVLOG(1) << mock_log_source.Log();
  DVLOG_IF(1, true) << mock_log_source.Log();
  DVPLOG(1) << mock_log_source.Log();
  DVPLOG_IF(1, true) << mock_log_source.Log();
}

TEST_F(LoggingTest, LoggingIsLazyByDestination) {
  MockLogSource mock_log_source;
  MockLogSource mock_log_source_error;
  EXPECT_CALL(mock_log_source, Log()).Times(0);

  // Severity >= ERROR is always printed to stderr.
  EXPECT_CALL(mock_log_source_error, Log()).Times(1).
      WillRepeatedly(Return("log message"));

  LoggingSettings settings;
  settings.logging_dest = LOG_NONE;
  InitLogging(settings);

  LOG(INFO) << mock_log_source.Log();
  LOG(WARNING) << mock_log_source.Log();
  LOG(ERROR) << mock_log_source_error.Log();
}

// Official builds have CHECKs directly call BreakDebugger.
#if !defined(OFFICIAL_BUILD)

// https://crbug.com/709067 tracks test flakiness on iOS.
#if defined(OS_IOS)
#define MAYBE_CheckStreamsAreLazy DISABLED_CheckStreamsAreLazy
#else
#define MAYBE_CheckStreamsAreLazy CheckStreamsAreLazy
#endif
TEST_F(LoggingTest, MAYBE_CheckStreamsAreLazy) {
  MockLogSource mock_log_source, uncalled_mock_log_source;
  EXPECT_CALL(mock_log_source, Log()).Times(8).
      WillRepeatedly(Return("check message"));
  EXPECT_CALL(uncalled_mock_log_source, Log()).Times(0);

  ScopedLogAssertHandler scoped_assert_handler(base::Bind(LogSink));

  CHECK(mock_log_source.Log()) << uncalled_mock_log_source.Log();
  PCHECK(!mock_log_source.Log()) << mock_log_source.Log();
  CHECK_EQ(mock_log_source.Log(), mock_log_source.Log())
      << uncalled_mock_log_source.Log();
  CHECK_NE(mock_log_source.Log(), mock_log_source.Log())
      << mock_log_source.Log();
}

#endif

#if defined(OFFICIAL_BUILD) && defined(OS_WIN)
NOINLINE void CheckContainingFunc(int death_location) {
  CHECK(death_location != 1);
  CHECK(death_location != 2);
  CHECK(death_location != 3);
}

int GetCheckExceptionData(EXCEPTION_POINTERS* p, DWORD* code, void** addr) {
  *code = p->ExceptionRecord->ExceptionCode;
  *addr = p->ExceptionRecord->ExceptionAddress;
  return EXCEPTION_EXECUTE_HANDLER;
}

TEST_F(LoggingTest, CheckCausesDistinctBreakpoints) {
  DWORD code1 = 0;
  DWORD code2 = 0;
  DWORD code3 = 0;
  void* addr1 = nullptr;
  void* addr2 = nullptr;
  void* addr3 = nullptr;

  // Record the exception code and addresses.
  __try {
    CheckContainingFunc(1);
  } __except (
      GetCheckExceptionData(GetExceptionInformation(), &code1, &addr1)) {
  }

  __try {
    CheckContainingFunc(2);
  } __except (
      GetCheckExceptionData(GetExceptionInformation(), &code2, &addr2)) {
  }

  __try {
    CheckContainingFunc(3);
  } __except (
      GetCheckExceptionData(GetExceptionInformation(), &code3, &addr3)) {
  }

  // Ensure that the exception codes are correct (in particular, breakpoints,
  // not access violations).
  EXPECT_EQ(STATUS_BREAKPOINT, code1);
  EXPECT_EQ(STATUS_BREAKPOINT, code2);
  EXPECT_EQ(STATUS_BREAKPOINT, code3);

  // Ensure that none of the CHECKs are colocated.
  EXPECT_NE(addr1, addr2);
  EXPECT_NE(addr1, addr3);
  EXPECT_NE(addr2, addr3);
}
#elif defined(OS_FUCHSIA)

// CHECK causes a direct crash (without jumping to another function) only in
// official builds. Unfortunately, continuous test coverage on official builds
// is lower. Furthermore, since the Fuchsia implementation uses threads, it is
// not possible to rely on an implementation of CHECK that calls abort(), which
// takes down the whole process, preventing the thread exception handler from
// handling the exception. DO_CHECK here falls back on IMMEDIATE_CRASH() in
// non-official builds, to catch regressions earlier in the CQ.
#if defined(OFFICIAL_BUILD)
#define DO_CHECK CHECK
#else
#define DO_CHECK(cond) \
  if (!(cond)) {       \
    IMMEDIATE_CRASH(); \
  }
#endif

static const unsigned int kExceptionPortKey = 1u;
static const unsigned int kThreadEndedPortKey = 2u;

struct thread_data_t {
  // For signaling the thread ended properly.
  zx::unowned_event event;
  // For registering thread termination.
  zx::unowned_port port;
  // Location where the thread is expected to crash.
  int death_location;
};

void* CrashThread(void* arg) {
  zx_status_t status;

  thread_data_t* data = (thread_data_t*)arg;
  int death_location = data->death_location;

  // Register the exception handler on the port.
  status = zx::thread::self()->bind_exception_port(*data->port,
                                                   kExceptionPortKey, 0);
  if (status != ZX_OK) {
    data->event->signal(0, ZX_USER_SIGNAL_0);
    return nullptr;
  }

  DO_CHECK(death_location != 1);
  DO_CHECK(death_location != 2);
  DO_CHECK(death_location != 3);

  // We should never reach this point, signal the thread incorrectly ended
  // properly.
  data->event->signal(0, ZX_USER_SIGNAL_0);
  return nullptr;
}

// Runs the CrashThread function in a separate thread.
void SpawnCrashThread(int death_location, uintptr_t* child_crash_addr) {
  zx::port port;
  zx::event event;
  zx_status_t status;

  status = zx::port::create(0, &port);
  ASSERT_EQ(status, ZX_OK);
  status = zx::event::create(0, &event);
  ASSERT_EQ(status, ZX_OK);

  // Register the thread ended event on the port.
  status = event.wait_async(port, kThreadEndedPortKey, ZX_USER_SIGNAL_0,
                            ZX_WAIT_ASYNC_ONCE);
  ASSERT_EQ(status, ZX_OK);

  // Run the thread.
  thread_data_t thread_data = {zx::unowned_event(event), zx::unowned_port(port),
                               death_location};
  pthread_t thread;
  int ret = pthread_create(&thread, nullptr, CrashThread, &thread_data);
  ASSERT_EQ(ret, 0);

  // Wait on the port.
  zx_port_packet_t packet;
  status = port.wait(zx::time::infinite(), &packet);
  ASSERT_EQ(status, ZX_OK);
  // Check the thread did crash and not terminate.
  ASSERT_EQ(packet.key, kExceptionPortKey);

  // Get the crash address.
  zx::thread zircon_thread;
  status = zx::process::self()->get_child(packet.exception.tid,
                                          ZX_RIGHT_SAME_RIGHTS, &zircon_thread);
  ASSERT_EQ(status, ZX_OK);
  zx_thread_state_general_regs_t buffer;
  status = zircon_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &buffer,
                                    sizeof(buffer));
  ASSERT_EQ(status, ZX_OK);
#if defined(ARCH_CPU_X86_64)
  *child_crash_addr = static_cast<uintptr_t>(buffer.rip);
#elif defined(ARCH_CPU_ARM64)
  *child_crash_addr = static_cast<uintptr_t>(buffer.pc);
#else
#error Unsupported architecture
#endif

  status = zircon_thread.kill();
  ASSERT_EQ(status, ZX_OK);
}

TEST_F(LoggingTest, CheckCausesDistinctBreakpoints) {
  uintptr_t child_crash_addr_1 = 0;
  uintptr_t child_crash_addr_2 = 0;
  uintptr_t child_crash_addr_3 = 0;

  SpawnCrashThread(1, &child_crash_addr_1);
  SpawnCrashThread(2, &child_crash_addr_2);
  SpawnCrashThread(3, &child_crash_addr_3);

  ASSERT_NE(0u, child_crash_addr_1);
  ASSERT_NE(0u, child_crash_addr_2);
  ASSERT_NE(0u, child_crash_addr_3);
  ASSERT_NE(child_crash_addr_1, child_crash_addr_2);
  ASSERT_NE(child_crash_addr_1, child_crash_addr_3);
  ASSERT_NE(child_crash_addr_2, child_crash_addr_3);
}
#elif defined(OS_POSIX) && !defined(OS_NACL) && !defined(OS_IOS) && \
    (defined(ARCH_CPU_X86_FAMILY) || defined(ARCH_CPU_ARM_FAMILY))

int g_child_crash_pipe;

void CheckCrashTestSighandler(int, siginfo_t* info, void* context_ptr) {
  // Conversely to what clearly stated in "man 2 sigaction", some Linux kernels
  // do NOT populate the |info->si_addr| in the case of a SIGTRAP. Hence we
  // need the arch-specific boilerplate below, which is inspired by breakpad.
  // At the same time, on OSX, ucontext.h is deprecated but si_addr works fine.
  uintptr_t crash_addr = 0;
#if defined(OS_MACOSX)
  crash_addr = reinterpret_cast<uintptr_t>(info->si_addr);
#else  // OS_POSIX && !OS_MACOSX
  ucontext_t* context = reinterpret_cast<ucontext_t*>(context_ptr);
#if defined(ARCH_CPU_X86)
  crash_addr = static_cast<uintptr_t>(context->uc_mcontext.gregs[REG_EIP]);
#elif defined(ARCH_CPU_X86_64)
  crash_addr = static_cast<uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(ARCH_CPU_ARMEL)
  crash_addr = static_cast<uintptr_t>(context->uc_mcontext.arm_pc);
#elif defined(ARCH_CPU_ARM64)
  crash_addr = static_cast<uintptr_t>(context->uc_mcontext.pc);
#endif  // ARCH_*
#endif  // OS_POSIX && !OS_MACOSX
  HANDLE_EINTR(write(g_child_crash_pipe, &crash_addr, sizeof(uintptr_t)));
  _exit(0);
}

// CHECK causes a direct crash (without jumping to another function) only in
// official builds. Unfortunately, continuous test coverage on official builds
// is lower. DO_CHECK here falls back on a home-brewed implementation in
// non-official builds, to catch regressions earlier in the CQ.
#if defined(OFFICIAL_BUILD)
#define DO_CHECK CHECK
#else
#define DO_CHECK(cond) \
  if (!(cond))         \
  IMMEDIATE_CRASH()
#endif

void CrashChildMain(int death_location) {
  struct sigaction act = {};
  act.sa_sigaction = CheckCrashTestSighandler;
  act.sa_flags = SA_SIGINFO;
  ASSERT_EQ(0, sigaction(SIGTRAP, &act, nullptr));
  ASSERT_EQ(0, sigaction(SIGBUS, &act, nullptr));
  ASSERT_EQ(0, sigaction(SIGILL, &act, nullptr));
  DO_CHECK(death_location != 1);
  DO_CHECK(death_location != 2);
  printf("\n");
  DO_CHECK(death_location != 3);

  // Should never reach this point.
  const uintptr_t failed = 0;
  HANDLE_EINTR(write(g_child_crash_pipe, &failed, sizeof(uintptr_t)));
};

void SpawnChildAndCrash(int death_location, uintptr_t* child_crash_addr) {
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));

  int pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {      // child process.
    close(pipefd[0]);  // Close reader (parent) end.
    g_child_crash_pipe = pipefd[1];
    CrashChildMain(death_location);
    FAIL() << "The child process was supposed to crash. It didn't.";
  }

  close(pipefd[1]);  // Close writer (child) end.
  DCHECK(child_crash_addr);
  int res = HANDLE_EINTR(read(pipefd[0], child_crash_addr, sizeof(uintptr_t)));
  ASSERT_EQ(static_cast<int>(sizeof(uintptr_t)), res);
}

TEST_F(LoggingTest, CheckCausesDistinctBreakpoints) {
  uintptr_t child_crash_addr_1 = 0;
  uintptr_t child_crash_addr_2 = 0;
  uintptr_t child_crash_addr_3 = 0;

  SpawnChildAndCrash(1, &child_crash_addr_1);
  SpawnChildAndCrash(2, &child_crash_addr_2);
  SpawnChildAndCrash(3, &child_crash_addr_3);

  ASSERT_NE(0u, child_crash_addr_1);
  ASSERT_NE(0u, child_crash_addr_2);
  ASSERT_NE(0u, child_crash_addr_3);
  ASSERT_NE(child_crash_addr_1, child_crash_addr_2);
  ASSERT_NE(child_crash_addr_1, child_crash_addr_3);
  ASSERT_NE(child_crash_addr_2, child_crash_addr_3);
}
#endif  // OS_POSIX

TEST_F(LoggingTest, DebugLoggingReleaseBehavior) {
#if DCHECK_IS_ON()
  int debug_only_variable = 1;
#endif
  // These should avoid emitting references to |debug_only_variable|
  // in release mode.
  DLOG_IF(INFO, debug_only_variable) << "test";
  DLOG_ASSERT(debug_only_variable) << "test";
  DPLOG_IF(INFO, debug_only_variable) << "test";
  DVLOG_IF(1, debug_only_variable) << "test";
}

TEST_F(LoggingTest, DcheckStreamsAreLazy) {
  MockLogSource mock_log_source;
  EXPECT_CALL(mock_log_source, Log()).Times(0);
#if DCHECK_IS_ON()
  DCHECK(true) << mock_log_source.Log();
  DCHECK_EQ(0, 0) << mock_log_source.Log();
#else
  DCHECK(mock_log_source.Log()) << mock_log_source.Log();
  DPCHECK(mock_log_source.Log()) << mock_log_source.Log();
  DCHECK_EQ(0, 0) << mock_log_source.Log();
  DCHECK_EQ(mock_log_source.Log(), static_cast<const char*>(nullptr))
      << mock_log_source.Log();
#endif
}

void DcheckEmptyFunction1() {
  // Provide a body so that Release builds do not cause the compiler to
  // optimize DcheckEmptyFunction1 and DcheckEmptyFunction2 as a single
  // function, which breaks the Dcheck tests below.
  LOG(INFO) << "DcheckEmptyFunction1";
}
void DcheckEmptyFunction2() {}

#if DCHECK_IS_CONFIGURABLE
class ScopedDcheckSeverity {
 public:
  ScopedDcheckSeverity(LogSeverity new_severity) : old_severity_(LOG_DCHECK) {
    LOG_DCHECK = new_severity;
  }

  ~ScopedDcheckSeverity() { LOG_DCHECK = old_severity_; }

 private:
  LogSeverity old_severity_;
};
#endif  // DCHECK_IS_CONFIGURABLE

// https://crbug.com/709067 tracks test flakiness on iOS.
#if defined(OS_IOS)
#define MAYBE_Dcheck DISABLED_Dcheck
#else
#define MAYBE_Dcheck Dcheck
#endif
TEST_F(LoggingTest, MAYBE_Dcheck) {
#if DCHECK_IS_CONFIGURABLE
  // DCHECKs are enabled, and LOG_DCHECK is mutable, but defaults to non-fatal.
  // Set it to LOG_FATAL to get the expected behavior from the rest of this
  // test.
  ScopedDcheckSeverity dcheck_severity(LOG_FATAL);
#endif  // DCHECK_IS_CONFIGURABLE

#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
  // Release build.
  EXPECT_FALSE(DCHECK_IS_ON());
  EXPECT_FALSE(DLOG_IS_ON(DCHECK));
#elif defined(NDEBUG) && defined(DCHECK_ALWAYS_ON)
  // Release build with real DCHECKS.
  ScopedLogAssertHandler scoped_assert_handler(base::Bind(LogSink));
  EXPECT_TRUE(DCHECK_IS_ON());
  EXPECT_TRUE(DLOG_IS_ON(DCHECK));
#else
  // Debug build.
  ScopedLogAssertHandler scoped_assert_handler(base::Bind(LogSink));
  EXPECT_TRUE(DCHECK_IS_ON());
  EXPECT_TRUE(DLOG_IS_ON(DCHECK));
#endif

  // DCHECKs are fatal iff they're compiled in DCHECK_IS_ON() and the DCHECK
  // log level is set to fatal.
  const bool dchecks_are_fatal = DCHECK_IS_ON() && LOG_DCHECK == LOG_FATAL;
  EXPECT_EQ(0, g_log_sink_call_count);
  DCHECK(false);
  EXPECT_EQ(dchecks_are_fatal ? 1 : 0, g_log_sink_call_count);
  DPCHECK(false);
  EXPECT_EQ(dchecks_are_fatal ? 2 : 0, g_log_sink_call_count);
  DCHECK_EQ(0, 1);
  EXPECT_EQ(dchecks_are_fatal ? 3 : 0, g_log_sink_call_count);

  // Test DCHECK on std::nullptr_t
  g_log_sink_call_count = 0;
  const void* p_null = nullptr;
  const void* p_not_null = &p_null;
  DCHECK_EQ(p_null, nullptr);
  DCHECK_EQ(nullptr, p_null);
  DCHECK_NE(p_not_null, nullptr);
  DCHECK_NE(nullptr, p_not_null);
  EXPECT_EQ(0, g_log_sink_call_count);

  // Test DCHECK on a scoped enum.
  enum class Animal { DOG, CAT };
  DCHECK_EQ(Animal::DOG, Animal::DOG);
  EXPECT_EQ(0, g_log_sink_call_count);
  DCHECK_EQ(Animal::DOG, Animal::CAT);
  EXPECT_EQ(dchecks_are_fatal ? 1 : 0, g_log_sink_call_count);

  // Test DCHECK on functions and function pointers.
  g_log_sink_call_count = 0;
  struct MemberFunctions {
    void MemberFunction1() {
      // See the comment in DcheckEmptyFunction1().
      LOG(INFO) << "Do not merge with MemberFunction2.";
    }
    void MemberFunction2() {}
  };
  void (MemberFunctions::*mp1)() = &MemberFunctions::MemberFunction1;
  void (MemberFunctions::*mp2)() = &MemberFunctions::MemberFunction2;
  void (*fp1)() = DcheckEmptyFunction1;
  void (*fp2)() = DcheckEmptyFunction2;
  void (*fp3)() = DcheckEmptyFunction1;
  DCHECK_EQ(fp1, fp3);
  EXPECT_EQ(0, g_log_sink_call_count);
  DCHECK_EQ(mp1, &MemberFunctions::MemberFunction1);
  EXPECT_EQ(0, g_log_sink_call_count);
  DCHECK_EQ(mp2, &MemberFunctions::MemberFunction2);
  EXPECT_EQ(0, g_log_sink_call_count);
  DCHECK_EQ(fp1, fp2);
  EXPECT_EQ(dchecks_are_fatal ? 1 : 0, g_log_sink_call_count);
  DCHECK_EQ(mp2, &MemberFunctions::MemberFunction1);
  EXPECT_EQ(dchecks_are_fatal ? 2 : 0, g_log_sink_call_count);
}

TEST_F(LoggingTest, DcheckReleaseBehavior) {
  int some_variable = 1;
  // These should still reference |some_variable| so we don't get
  // unused variable warnings.
  DCHECK(some_variable) << "test";
  DPCHECK(some_variable) << "test";
  DCHECK_EQ(some_variable, 1) << "test";
}

TEST_F(LoggingTest, DCheckEqStatements) {
  bool reached = false;
  if (false)
    DCHECK_EQ(false, true);           // Unreached.
  else
    DCHECK_EQ(true, reached = true);  // Reached, passed.
  ASSERT_EQ(DCHECK_IS_ON() ? true : false, reached);

  if (false)
    DCHECK_EQ(false, true);           // Unreached.
}

TEST_F(LoggingTest, CheckEqStatements) {
  bool reached = false;
  if (false)
    CHECK_EQ(false, true);           // Unreached.
  else
    CHECK_EQ(true, reached = true);  // Reached, passed.
  ASSERT_TRUE(reached);

  if (false)
    CHECK_EQ(false, true);           // Unreached.
}

TEST_F(LoggingTest, NestedLogAssertHandlers) {
  ::testing::InSequence dummy;
  ::testing::StrictMock<MockLogAssertHandler> handler_a, handler_b;

  EXPECT_CALL(
      handler_a,
      HandleLogAssert(
          _, _, base::StringPiece("First assert must be caught by handler_a"),
          _));
  EXPECT_CALL(
      handler_b,
      HandleLogAssert(
          _, _, base::StringPiece("Second assert must be caught by handler_b"),
          _));
  EXPECT_CALL(
      handler_a,
      HandleLogAssert(
          _, _,
          base::StringPiece("Last assert must be caught by handler_a again"),
          _));

  logging::ScopedLogAssertHandler scoped_handler_a(base::Bind(
      &MockLogAssertHandler::HandleLogAssert, base::Unretained(&handler_a)));

  // Using LOG(FATAL) rather than CHECK(false) here since log messages aren't
  // preserved for CHECKs in official builds.
  LOG(FATAL) << "First assert must be caught by handler_a";

  {
    logging::ScopedLogAssertHandler scoped_handler_b(base::Bind(
        &MockLogAssertHandler::HandleLogAssert, base::Unretained(&handler_b)));
    LOG(FATAL) << "Second assert must be caught by handler_b";
  }

  LOG(FATAL) << "Last assert must be caught by handler_a again";
}

// Test that defining an operator<< for a type in a namespace doesn't prevent
// other code in that namespace from calling the operator<<(ostream, wstring)
// defined by logging.h. This can fail if operator<<(ostream, wstring) can't be
// found by ADL, since defining another operator<< prevents name lookup from
// looking in the global namespace.
namespace nested_test {
  class Streamable {};
  ALLOW_UNUSED_TYPE std::ostream& operator<<(std::ostream& out,
                                             const Streamable&) {
    return out << "Streamable";
  }
  TEST_F(LoggingTest, StreamingWstringFindsCorrectOperator) {
    std::wstring wstr = L"Hello World";
    std::ostringstream ostr;
    ostr << wstr;
    EXPECT_EQ("Hello World", ostr.str());
  }
}  // namespace nested_test

#if DCHECK_IS_CONFIGURABLE
TEST_F(LoggingTest, ConfigurableDCheck) {
  // Verify that DCHECKs default to non-fatal in configurable-DCHECK builds.
  // Note that we require only that DCHECK is non-fatal by default, rather
  // than requiring that it be exactly INFO, ERROR, etc level.
  EXPECT_LT(LOG_DCHECK, LOG_FATAL);
  DCHECK(false);

  // Verify that DCHECK* aren't hard-wired to crash on failure.
  LOG_DCHECK = LOG_INFO;
  DCHECK(false);
  DCHECK_EQ(1, 2);

  // Verify that DCHECK does crash if LOG_DCHECK is set to LOG_FATAL.
  LOG_DCHECK = LOG_FATAL;

  ::testing::StrictMock<MockLogAssertHandler> handler;
  EXPECT_CALL(handler, HandleLogAssert(_, _, _, _)).Times(2);
  {
    logging::ScopedLogAssertHandler scoped_handler_b(base::Bind(
        &MockLogAssertHandler::HandleLogAssert, base::Unretained(&handler)));
    DCHECK(false);
    DCHECK_EQ(1, 2);
  }
}

TEST_F(LoggingTest, ConfigurableDCheckFeature) {
  // Initialize FeatureList with and without DcheckIsFatal, and verify the
  // value of LOG_DCHECK. Note that we don't require that DCHECK take a
  // specific value when the feature is off, only that it is non-fatal.

  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitFromCommandLine("DcheckIsFatal", "");
    EXPECT_EQ(LOG_DCHECK, LOG_FATAL);
  }

  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitFromCommandLine("", "DcheckIsFatal");
    EXPECT_LT(LOG_DCHECK, LOG_FATAL);
  }

  // The default case is last, so we leave LOG_DCHECK in the default state.
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitFromCommandLine("", "");
    EXPECT_LT(LOG_DCHECK, LOG_FATAL);
  }
}
#endif  // DCHECK_IS_CONFIGURABLE

#if defined(OS_FUCHSIA)
TEST_F(LoggingTest, FuchsiaLogging) {
  MockLogSource mock_log_source;
  EXPECT_CALL(mock_log_source, Log())
      .Times(DCHECK_IS_ON() ? 2 : 1)
      .WillRepeatedly(Return("log message"));

  SetMinLogLevel(LOG_INFO);

  EXPECT_TRUE(LOG_IS_ON(INFO));
  EXPECT_TRUE((DCHECK_IS_ON() != 0) == DLOG_IS_ON(INFO));

  ZX_LOG(INFO, ZX_ERR_INTERNAL) << mock_log_source.Log();
  ZX_DLOG(INFO, ZX_ERR_INTERNAL) << mock_log_source.Log();

  ZX_CHECK(true, ZX_ERR_INTERNAL);
  ZX_DCHECK(true, ZX_ERR_INTERNAL);
}
#endif  // defined(OS_FUCHSIA)

TEST_F(LoggingTest, LogPrefix) {
  // Set up a callback function to capture the log output string.
  auto old_log_message_handler = GetLogMessageHandler();
  // Use a static because only captureless lambdas can be converted to a
  // function pointer for SetLogMessageHandler().
  static std::string* log_string_ptr = nullptr;
  std::string log_string;
  log_string_ptr = &log_string;
  SetLogMessageHandler([](int severity, const char* file, int line,
                          size_t start, const std::string& str) -> bool {
    *log_string_ptr = str;
    return true;
  });

  // Logging with a prefix includes the prefix string after the opening '['.
  const char kPrefix[] = "prefix";
  SetLogPrefix(kPrefix);
  LOG(ERROR) << "test";  // Writes into |log_string|.
  EXPECT_EQ(1u, log_string.find(kPrefix));

  // Logging without a prefix does not include the prefix string.
  SetLogPrefix(nullptr);
  LOG(ERROR) << "test";  // Writes into |log_string|.
  EXPECT_EQ(std::string::npos, log_string.find(kPrefix));

  // Clean up.
  SetLogMessageHandler(old_log_message_handler);
  log_string_ptr = nullptr;
}

#if !defined(ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER)
// Since we scan potentially uninitialized portions of the stack, we can't run
// this test under any sanitizer that checks for uninitialized reads.
TEST_F(LoggingTest, LogMessageMarkersOnStack) {
  const uint32_t kLogStartMarker = 0xbedead01;
  const uint32_t kLogEndMarker = 0x5050dead;
  const char kTestMessage[] = "Oh noes! I have crashed! 💩";

  uint32_t stack_start = 0;

  // Install a LogAssertHandler which will scan between |stack_start| and its
  // local-scope stack for the start & end markers, and verify the message.
  ScopedLogAssertHandler assert_handler(base::BindRepeating(
      [](uint32_t* stack_start_ptr, const char* file, int line,
         const base::StringPiece message, const base::StringPiece stack_trace) {
        uint32_t stack_end;
        uint32_t* stack_end_ptr = &stack_end;

        // Scan the stack for the expected markers.
        uint32_t* start_marker = nullptr;
        uint32_t* end_marker = nullptr;
        for (uint32_t* ptr = stack_end_ptr; ptr <= stack_start_ptr; ++ptr) {
          if (*ptr == kLogStartMarker)
            start_marker = ptr;
          else if (*ptr == kLogEndMarker)
            end_marker = ptr;
        }

        // Verify that start & end markers were found, somewhere, in-between
        // this and the LogAssertHandler scope, in the LogMessage destructor's
        // stack frame.
        ASSERT_TRUE(start_marker);
        ASSERT_TRUE(end_marker);

        // Verify that the |message| is found in-between the markers.
        const char* start_char_marker =
            reinterpret_cast<char*>(start_marker + 1);
        const char* end_char_marker = reinterpret_cast<char*>(end_marker);

        const base::StringPiece stack_view(start_char_marker,
                                           end_char_marker - start_char_marker);
        ASSERT_FALSE(stack_view.find(message) == base::StringPiece::npos);
      },
      &stack_start));

  // Trigger a log assertion, with a test message we can check for.
  LOG(FATAL) << kTestMessage;
}
#endif  // !defined(ADDRESS_SANITIZER)

}  // namespace

}  // namespace logging
