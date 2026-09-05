// GeneralsX @feature Codex 05/09/2026 Opt-in WebAssembly surface ownership diagnostics.
#pragma once

#ifdef __EMSCRIPTEN__
enum SurfaceTraceOperation { SURFACE_TRACE_VALIDATE, SURFACE_TRACE_BIND, SURFACE_TRACE_RELEASE };
extern "C" void Igroteka_TraceSurfaceOwner(const void *owner, const void *surface, SurfaceTraceOperation operation);
extern "C" void Igroteka_TraceSurfaceWindow(const char *name);
#endif
