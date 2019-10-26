/*
 * Copyright (c) 2000, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "jvm.h"
#include "jvmci/jvmci_globals.hpp"
#include "gc/shared/gcConfig.hpp"
#include "utilities/defaultStream.hpp"
#include "utilities/ostream.hpp"
#include "runtime/globals_extension.hpp"

JVMCI_FLAGS(MATERIALIZE_DEVELOPER_FLAG, \
            MATERIALIZE_PD_DEVELOPER_FLAG, \
            MATERIALIZE_PRODUCT_FLAG, \
            MATERIALIZE_PD_PRODUCT_FLAG, \
            MATERIALIZE_DIAGNOSTIC_FLAG, \
            MATERIALIZE_PD_DIAGNOSTIC_FLAG, \
            MATERIALIZE_EXPERIMENTAL_FLAG, \
            MATERIALIZE_NOTPRODUCT_FLAG,
            IGNORE_RANGE, \
            IGNORE_CONSTRAINT, \
            IGNORE_WRITEABLE)

fileStream* JVMCIGlobals::_jni_config_file = NULL;

// Gets the value of the jvmci.Compiler system property, initializing it
// from <java.home>/lib/jvmci/compiler-name if the property is not
// already defined and the compiler-name file exists.
static const char* get_jvmci_compiler_name(bool* error) {
  *error = false;
  const char* compiler_name = Arguments::get_property("jvmci.Compiler");
  if (compiler_name == NULL) {
    char filename[JVM_MAXPATHLEN];
    const char* fileSep = os::file_separator();
    jio_snprintf(filename, sizeof(filename), "%s%slib%sjvmci%scompiler-name", Arguments::get_java_home(), fileSep, fileSep, fileSep);
    struct stat statbuf;
    if (os::stat(filename, &statbuf) == 0) {
      char line[256];
      if ((size_t) statbuf.st_size > sizeof(line)) {
        jio_fprintf(defaultStream::error_stream(), "Size of %s is greater than %d\n", filename, sizeof(line));
        *error = true;
        return NULL;
      }

      FILE* stream = fopen(filename, "r");
      if (stream != NULL) {
        if (fgets(line, sizeof(line), stream) != NULL) {
          // Strip newline from end of the line
          char* p = line + strlen(line) - 1;
          while (p >= line && (*p == '\r' || *p == '\n')) {
            *p-- = 0;
          }
          SystemProperty* last_prop = NULL;
          for (SystemProperty* p = Arguments::system_properties(); p != NULL; p = p->next()) {
            last_prop = p;
          }
          guarantee(last_prop != NULL, "Cannot set jvmci.Compiler property before system properties have been created");
          SystemProperty* new_p = new SystemProperty("jvmci.Compiler", line, true);
          last_prop->set_next(new_p);
          compiler_name = new_p->value();
        } else {
          jio_fprintf(defaultStream::error_stream(),
              "Failed to read from %s (errno = %d)\n", filename, errno);
          fclose(stream);
          *error = true;
          return NULL;
        }
        fclose(stream);
      } else {
        jio_fprintf(defaultStream::error_stream(),
            "Failed to open %s (errno = %d)\n", filename, errno);
        *error = true;
        return NULL;
      }
    }
  }
  return compiler_name;
}

// Return true if jvmci flags are consistent.
bool JVMCIGlobals::check_jvmci_flags_are_consistent() {

#ifndef PRODUCT
#define APPLY_JVMCI_FLAGS(params3, params4) \
  JVMCI_FLAGS(params4, params3, params4, params3, params4, params3, params4, params4, IGNORE_RANGE, IGNORE_CONSTRAINT, IGNORE_WRITEABLE)
#define JVMCI_DECLARE_CHECK4(type, name, value, doc) bool name##checked = false;
#define JVMCI_DECLARE_CHECK3(type, name, doc)        bool name##checked = false;
#define JVMCI_FLAG_CHECKED(name)                          name##checked = true;
  APPLY_JVMCI_FLAGS(JVMCI_DECLARE_CHECK3, JVMCI_DECLARE_CHECK4)
#else
#define JVMCI_FLAG_CHECKED(name)
#endif

  // Checks that a given flag is not set if a given guard flag is false.
#define CHECK_NOT_SET(FLAG, GUARD)                     \
  JVMCI_FLAG_CHECKED(FLAG)                             \
  if (!GUARD && !FLAG_IS_DEFAULT(FLAG)) {              \
    jio_fprintf(defaultStream::error_stream(),         \
        "Improperly specified VM option '%s': '%s' must be enabled\n", #FLAG, #GUARD); \
    return false;                                      \
  }

  bool error;
  const char* compiler_name = get_jvmci_compiler_name(&error);
  if (error) {
    return false;
  }

  if (FLAG_IS_DEFAULT(UseJVMCICompiler) && !UseJVMCICompiler) {
    if (compiler_name != NULL) {
      // If a JVMCI compiler has been explicitly specified, then
      // we enable the JVMCI compiler by default.
      FLAG_SET_DEFAULT(UseJVMCICompiler, true);
    }
  }

  if (FLAG_IS_DEFAULT(UseJVMCINativeLibrary) && !UseJVMCINativeLibrary) {
    char path[JVM_MAXPATHLEN];
    if (os::dll_locate_lib(path, sizeof(path), Arguments::get_dll_dir(), JVMCI_SHARED_LIBRARY_NAME)) {
      struct stat statbuf;
      if (os::stat(path, &statbuf) == 0) {
        // If a JVMCI native library is present,
        // we enable UseJVMCINativeLibrary by default.
        FLAG_SET_DEFAULT(UseJVMCINativeLibrary, true);
      }
    }
  }

  JVMCI_FLAG_CHECKED(UseJVMCICompiler)
  JVMCI_FLAG_CHECKED(EnableJVMCI)
  JVMCI_FLAG_CHECKED(EnableJVMCIProduct)

  CHECK_NOT_SET(BootstrapJVMCI,   UseJVMCICompiler)
  CHECK_NOT_SET(PrintBootstrap,   UseJVMCICompiler)
  CHECK_NOT_SET(JVMCIThreads,     UseJVMCICompiler)
  CHECK_NOT_SET(JVMCIHostThreads, UseJVMCICompiler)

  if (UseJVMCICompiler) {
    if (FLAG_IS_DEFAULT(UseJVMCINativeLibrary) && !UseJVMCINativeLibrary) {
      char path[JVM_MAXPATHLEN];
      if (os::dll_locate_lib(path, sizeof(path), Arguments::get_dll_dir(), JVMCI_SHARED_LIBRARY_NAME)) {
        // If a JVMCI native library is present,
        // we enable UseJVMCINativeLibrary by default.
        FLAG_SET_DEFAULT(UseJVMCINativeLibrary, true);
      }
    }
    if (!FLAG_IS_DEFAULT(EnableJVMCI) && !EnableJVMCI) {
      jio_fprintf(defaultStream::error_stream(),
          "Improperly specified VM option UseJVMCICompiler: EnableJVMCI cannot be disabled\n");
      return false;
    }
    FLAG_SET_DEFAULT(EnableJVMCI, true);
    if (BootstrapJVMCI && UseJVMCINativeLibrary) {
      jio_fprintf(defaultStream::error_stream(), "-XX:+BootstrapJVMCI is not compatible with -XX:+UseJVMCINativeLibrary\n");
      return false;
    }
  }

  if (!EnableJVMCI) {
    // Switch off eager JVMCI initialization if JVMCI is disabled.
    // Don't throw error if EagerJVMCI is set to allow testing.
    if (EagerJVMCI) {
      FLAG_SET_DEFAULT(EagerJVMCI, false);
    }
  }
  JVMCI_FLAG_CHECKED(EagerJVMCI)

  CHECK_NOT_SET(JVMCITraceLevel,              EnableJVMCI)
  CHECK_NOT_SET(JVMCICounterSize,             EnableJVMCI)
  CHECK_NOT_SET(JVMCICountersExcludeCompiler, EnableJVMCI)
  CHECK_NOT_SET(JVMCIUseFastLocking,          EnableJVMCI)
  CHECK_NOT_SET(JVMCINMethodSizeLimit,        EnableJVMCI)
  CHECK_NOT_SET(MethodProfileWidth,           EnableJVMCI)
  CHECK_NOT_SET(JVMCIPrintProperties,         EnableJVMCI)
  CHECK_NOT_SET(UseJVMCINativeLibrary,        EnableJVMCI)
  CHECK_NOT_SET(JVMCILibPath,                 EnableJVMCI)
  CHECK_NOT_SET(JVMCILibDumpJNIConfig,        EnableJVMCI)

#ifndef PRODUCT
#define JVMCI_CHECK4(type, name, value, doc) assert(name##checked, #name " flag not checked");
#define JVMCI_CHECK3(type, name, doc)        assert(name##checked, #name " flag not checked");
  // Ensures that all JVMCI flags are checked by this method.
  APPLY_JVMCI_FLAGS(JVMCI_CHECK3, JVMCI_CHECK4)
#undef APPLY_JVMCI_FLAGS
#undef JVMCI_DECLARE_CHECK3
#undef JVMCI_DECLARE_CHECK4
#undef JVMCI_CHECK3
#undef JVMCI_CHECK4
#undef JVMCI_FLAG_CHECKED
#endif // PRODUCT
#undef CHECK_NOT_SET

  if (JVMCILibDumpJNIConfig != NULL) {
    _jni_config_file = new(ResourceObj::C_HEAP, mtJVMCI) fileStream(JVMCILibDumpJNIConfig);
    if (_jni_config_file == NULL || !_jni_config_file->is_open()) {
      jio_fprintf(defaultStream::error_stream(),
          "Could not open file for dumping JVMCI shared library JNI config: %s\n", JVMCILibDumpJNIConfig);
      return false;
    }
  }

  return true;
}

// Convert JVMCI flags from experimental to product
bool JVMCIGlobals::enable_jvmci_product_mode(JVMFlag::Flags origin) {
  const char *JVMCIFlags[] = {
    "EnableJVMCI",
    "EnableJVMCIProduct",
    "UseJVMCICompiler",
    "JVMCIPrintProperties",
    "EagerJVMCI",
    "JVMCIThreads",
    "JVMCICounterSize",
    "JVMCICountersExcludeCompiler",
    "JVMCINMethodSizeLimit",
    "JVMCILibPath",
    "JVMCILibDumpJNIConfig",
    "UseJVMCINativeLibrary",
    NULL
  };

  for (int i = 0; JVMCIFlags[i] != NULL; i++) {
    JVMFlag *jvmciFlag = JVMFlag::find_flag(JVMCIFlags[i]);
    if (jvmciFlag == NULL) {
      return false;
    }
    jvmciFlag->clear_experimental();
  }

  bool value = true;
  if (JVMFlag::boolAtPut("EnableJVMCIProduct", &value, origin) != JVMFlag::SUCCESS) {
    return false;
  }
  value = true;
  if (JVMFlag::boolAtPut("UseJVMCICompiler", &value, origin) != JVMFlag::SUCCESS) {
    return false;
  }

  return true;
}


void JVMCIGlobals::check_jvmci_supported_gc() {
  if (EnableJVMCI) {
    // Check if selected GC is supported by JVMCI and Java compiler
    if (!(UseSerialGC || UseParallelGC || UseParallelOldGC || UseG1GC)) {
      vm_exit_during_initialization("JVMCI Compiler does not support selected GC", GCConfig::hs_err_name());
      FLAG_SET_DEFAULT(EnableJVMCI, false);
      FLAG_SET_DEFAULT(UseJVMCICompiler, false);
    }
  }
}
