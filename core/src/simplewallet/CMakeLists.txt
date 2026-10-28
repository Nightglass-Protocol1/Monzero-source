# Copyright (c) 2014-2022, The Monero Project
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set(simplewallet_sources
  simplewallet.cpp)

set(simplewallet_headers)

monero_find_all_headers(simplewallet_private_headers "${CMAKE_CURRENT_SOURCE_DIR}")

monero_private_headers(simplewallet
  ${simplewallet_private_headers})
monero_add_executable(simplewallet
  ${simplewallet_sources}
  ${simplewallet_headers}
  ${simplewallet_private_headers})
if(STATIC AND NOT MINGW)
  find_package(ZLIB REQUIRED)
  find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
endif()
target_link_libraries(simplewallet
  PRIVATE
    wallet
    rpc_base
    cryptonote_core
    cncrypto
    common
    mnemonics
    ${EPEE_READLINE}
    qrcodegen
    version
    ${Boost_CHRONO_LIBRARY}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_LOCALE_LIBRARY}
    ${ICU_LIBRARIES}
    ${Boost_THREAD_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
    ${GNU_READLINE_LIBRARY}
    ${EXTRA_LIBRARIES}
    $<$<AND:$<BOOL:${STATIC}>,$<NOT:$<BOOL:${MINGW}>>>:ZLIB::ZLIB>)
if(STATIC AND NOT MINGW)
  # OpenSSL's static archive references optional compression backends. Force
  # their objects into the final CLI wallet even though CMake emits OpenSSL
  # later through transitive target dependencies.
  target_link_options(simplewallet PRIVATE
    -Wl,--whole-archive
    ${ZLIB_LIBRARIES}
    ${ZSTD_LIBRARY}
    -Wl,--no-whole-archive)
endif()
set_property(TARGET simplewallet
  PROPERTY
    OUTPUT_NAME "monzero-wallet-cli")
install(TARGETS simplewallet DESTINATION bin)
