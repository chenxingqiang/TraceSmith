#include "tracesmith/common/stack_capture.hpp"
#include <cstring>
#include <sstream>
#include <thread>

// Platform-specific includes
#ifdef TRACESMITH_USE_LIBUNWIND
    #define UNW_LOCAL_ONLY
    #include <libunwind.h>
#endif

#ifdef __APPLE__
    #include <execinfo.h>
    #include <dlfcn.h>
    #include <cxxabi.h>
    #include <pthread.h>
#elif defined(__linux__)
    #include <execinfo.h>
    #include <dlfcn.h>
    #include <cxxabi.h>
    #include <unistd.h>
    #include <sys/syscall.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
#endif

namespace tracesmith {

// ============================================================================
// Platform Detection
// ============================================================================

bool StackCapture::isAvailable() {
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    return true;
#else
    return false;
#endif
}

uint64_t StackCapture::getCurrentThreadId() {
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(_WIN32)
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
}

// ============================================================================
// StackCapture Implementation
// ============================================================================

StackCapture::StackCapture(const StackCaptureConfig& config)
    : config_(config) {
}

size_t StackCapture::capture(CallStack& out) {
    return captureWithThreadId(getCurrentThreadId(), out);
}

size_t StackCapture::captureWithThreadId(uint64_t thread_id, CallStack& out) {
    out.thread_id = thread_id;
    out.frames.clear();
    
    // Capture raw addresses
    std::vector<void*> addresses(config_.max_depth);
    size_t count = captureImpl(addresses.data(), config_.max_depth);
    
    if (count == 0) {
        return 0;
    }
    
    // Skip requested frames (use parentheses to avoid Windows min/max macro issues)
    size_t skip = (std::min)(static_cast<size_t>(config_.skip_frames), count);
    
    // Convert to StackFrame
    out.frames.reserve(count - skip);
    for (size_t i = skip; i < count; ++i) {
        StackFrame frame;
        frame.address = reinterpret_cast<uint64_t>(addresses[i]);
        
        if (config_.resolve_symbols) {
            resolveAddress(frame.address, frame);
        }
        
        out.frames.push_back(frame);
    }
    
    return out.frames.size();
}

size_t StackCapture::captureImpl(void** addresses, size_t max_depth) {
#ifdef TRACESMITH_USE_LIBUNWIND
    // Use libunwind (preferred - cross-platform, robust)
    unw_context_t context;
    unw_cursor_t cursor;
    
    if (unw_getcontext(&context) != 0) {
        return 0;
    }
    
    if (unw_init_local(&cursor, &context) != 0) {
        return 0;
    }
    
    size_t count = 0;
    while (count < max_depth) {
        unw_word_t ip;
        if (unw_get_reg(&cursor, UNW_REG_IP, &ip) != 0) {
            break;
        }
        
        addresses[count++] = reinterpret_cast<void*>(ip);
        
        if (unw_step(&cursor) <= 0) {
            break;
        }
    }
    
    return count;
    
#elif defined(__APPLE__) || defined(__linux__)
    // Fallback: Use backtrace() on Unix-like systems
    int count = backtrace(addresses, static_cast<int>(max_depth));
    return count > 0 ? static_cast<size_t>(count) : 0;
    
#elif defined(_WIN32)
    // Use CaptureStackBackTrace() on Windows
    USHORT count = CaptureStackBackTrace(
        0,                              // Skip frames
        static_cast<ULONG>(max_depth),  // Max frames
        addresses,                      // Buffer
        nullptr                         // Hash (not used)
    );
    return static_cast<size_t>(count);
    
#else
    // No stack capture available
    (void)addresses;
    (void)max_depth;
    return 0;
#endif
}

bool StackCapture::resolveSymbols(CallStack& stack) {
    bool any_resolved = false;
    
    for (auto& frame : stack.frames) {
        if (resolveAddress(frame.address, frame)) {
            any_resolved = true;
        }
    }
    
    return any_resolved;
}

bool StackCapture::resolveAddress(uint64_t address, StackFrame& frame) {
#if defined(__APPLE__) || defined(__linux__)
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(address), &info) == 0) {
        return false;
    }
    
    // Get symbol name
    if (info.dli_sname) {
        if (config_.demangle) {
            frame.function_name = demangleSymbol(info.dli_sname);
        } else {
            frame.function_name = info.dli_sname;
        }
    }
    
    // Get file name (library/executable)
    if (info.dli_fname) {
        frame.file_name = info.dli_fname;
        
        // Extract just the filename
        const char* slash = strrchr(info.dli_fname, '/');
        if (slash) {
            frame.file_name = slash + 1;
        }
    }
    
    return !frame.function_name.empty();
    
#elif defined(_WIN32)
    HANDLE process = GetCurrentProcess();
    
    // Initialize symbol handler
    static bool initialized = false;
    if (!initialized) {
        SymInitialize(process, nullptr, TRUE);
        initialized = true;
    }
    
    // Get symbol info
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, symbol)) {
        frame.function_name = symbol->Name;
        
        // Try to get file/line info
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD line_disp = 0;
        
        if (SymGetLineFromAddr64(process, address, &line_disp, &line)) {
            frame.file_name = line.FileName;
            frame.line_number = line.LineNumber;
        }
        
        return true;
    }
    
    return false;
    
#else
    (void)address;
    (void)frame;
    return false;
#endif
}

std::string StackCapture::demangleSymbol(const char* mangled) {
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    
    if (status == 0 && demangled) {
        std::string result(demangled);
        free(demangled);
        return result;
    }
#endif
    
    // Return original if demangling failed
    return mangled;
}

} // namespace tracesmith
