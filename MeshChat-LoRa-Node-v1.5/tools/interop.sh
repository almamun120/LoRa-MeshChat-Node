#!/bin/bash
# Runs the app's REAL Kotlin core against the C++ relay core (needs the Kotlin build of the app core in ../../out_u, see README).
set -e
cd "$(dirname "$0")/.."
L=${GRADLE_LIB:-/opt/gradle-8.14.3/lib}
CP=build:${APP_CORE_CLASSES:-../out_u}:$L/kotlin-stdlib-2.0.21.jar:$L/kotlinx-coroutines-core-jvm-1.6.4.jar
java -cp $CP InteropKt gen build/kt_out.txt
(cd test && make interop >/dev/null && ./interop ../build/kt_out.txt ../build/cpp_out.txt)
java -Dfile.encoding=UTF-8 -cp $CP InteropKt verify build/cpp_out.txt build/kt_out.txt
