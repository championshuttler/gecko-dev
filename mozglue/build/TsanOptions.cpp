/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"

#ifndef _MSC_VER  // Not supported by clang-cl yet

//
// When running with ThreadSanitizer, we need to explicitly set some
// options specific to our codebase to prevent errors during runtime.
// To override these, set the TSAN_OPTIONS environment variable.
//
// Currently, these are:
//
//   abort_on_error=1 - Causes TSan to abort instead of using exit().
//   halt_on_error=1 - Causes TSan to stop on the first race detected.
//
//   report_signal_unsafe=0 - Required to avoid TSan deadlocks when
//   receiving external signals (e.g. SIGINT manually on console).
//
extern "C" const char* __tsan_default_options() {
  return "halt_on_error=1:abort_on_error=1:report_signal_unsafe=0";
}

//
// When running with ThreadSanitizer, we sometimes need to suppress existing
// races. However, in any case, it should be either because
//
//    1) a bug is on file. In this case, the bug number should always be
//       included with the suppression.
//
// or 2) this is an intentional race. Please be very careful with judging
//       races as intentional and benign. Races in C++ are undefined behavior
//       and compilers increasingly rely on exploiting this for optimizations.
//       Hence, many seemingly benign races cause harmful or unexpected
//       side-effects.
//
//       See also:
//       https://software.intel.com/en-us/blogs/2013/01/06/benign-data-races-what-could-possibly-go-wrong
//
//
// Also, when adding any race suppressions here, make sure to always add
// a signature for each of the two race stacks. Sometimes, TSan fails to
// symbolize one of the two traces and this can cause suppressed races to
// show up intermittently.
//
extern "C" const char* __tsan_default_suppressions() {
  return "# Add your suppressions below\n"

         // External uninstrumented libraries
         "called_from_lib:libatk-1\n"
         "called_from_lib:libcairo.so\n"
         "called_from_lib:libcairo-gobject\n"
         "called_from_lib:libgdk-3\n"
         "called_from_lib:libgdk_pixbuf\n"
         "called_from_lib:libgdk-x11\n"
         "called_from_lib:libgio-2\n"
         "called_from_lib:libglib-1\n"
         "called_from_lib:libglib-2\n"
         "called_from_lib:libgobject\n"
         "called_from_lib:libgtk-3\n"
         "called_from_lib:libgtk-x11\n"
         "called_from_lib:libgvfscommon\n"
         "called_from_lib:libgvfsdbus\n"
         "called_from_lib:libibus-1\n"
         "called_from_lib:libogg.so\n"
         "called_from_lib:libpangocairo\n"
         "called_from_lib:libpangoft2\n"
         "called_from_lib:pango-basic-fc\n"
         "called_from_lib:libpixman-1\n"
         "called_from_lib:libpulse.so\n"
         "called_from_lib:libpulsecommon\n"
         "called_from_lib:libsecret-1\n"
         "called_from_lib:libunity-gtk3-parser\n"
         "called_from_lib:libvorbis.so\n"
         "called_from_lib:libvorbisfile\n"
         "called_from_lib:libX11.so\n"
         "called_from_lib:libX11-xcb\n"
         "called_from_lib:libXau\n"
         "called_from_lib:libxcb.so\n"
         "called_from_lib:libXcomposite\n"
         "called_from_lib:libXcursor\n"
         "called_from_lib:libXdamage\n"
         "called_from_lib:libXdmcp\n"
         "called_from_lib:libXext\n"
         "called_from_lib:libXfixes\n"
         "called_from_lib:libXi.so\n"
         "called_from_lib:libXrandr\n"
         "called_from_lib:libXrender\n"
         "called_from_lib:libXss\n"

         // These libraries are uninstrumented and cause mutex false positives.
         // However, they can be unloaded by GTK early which we cannot avoid.
         "mutex:libGL.so\n"
         "mutex:libGLdispatch\n"
         "mutex:libGLX\n"

         // For some reason, the suppressions on libpulse.so
         // through `called_from_lib` only work partially.
         "race:libpulse.so\n"
         "race:pa_context_suspend_source_by_index\n"
         "race:pa_context_unref\n"
         "race:pa_format_info_set_prop_string_array\n"
         "race:pa_stream_get_index\n"
         "race:pa_stream_update_timing_info\n"

         // This is a callback from libglib-2 that is apparently
         // not fully suppressed through `called_from_lib`.
         "race:g_main_context_dispatch\n"

         // TSan internals
         "race:__tsan::ProcessPendingSignals\n"
         "race:__tsan::CallUserSignalHandler\n"

         // Benign read/write races on bitfields
         //
         // WARNING: Bitfield races are only benign if one of the concurrent
         // accesses is a read. Write/write races on different parts of a
         // bitfield can have severe side-effects.
         "race:WalkDiskCacheRunnable::Run\n"
         // Modifying `mResolveAgain` while reading `mGetTtl`
         "race:RemoveOrRefresh\n"
         "race:nsHostResolver::ThreadFunc\n"
         // Another bitfield access, confirmed benign. Bug 1614697 - permanent.
         "race:nsHttpChannel::OnCacheEntryCheck\n"
         "race:~AutoCacheWaitFlags\n"

         // Bug 1153409
         "race:third_party/sqlite3/*\n"
         "deadlock:third_party/sqlite3/*\n"

         // Bug 1367344
         "race:TelemetryImpl::sTelemetry\n"

         // Bug 1506812
         "race:BeginBackgroundRead\n"

         // Bug 1506910
         "race:gMozillaPoisonValue\n"

         // Bug 1587510
         "race:SystemGroupImpl::sSingleton\n"

         // Bug 1587513
         "race:std::sync::mutex::Mutex\n"

         // Bug 1590423 - permanent
         "race:sync..Arc\n"
         "race:alloc::sync::Arc\n"

         // Bug 1598342
         "race:_PR_DestroyThreadPrivate\n"
         "race:_pt_thread_death_internal\n"
         "race:nsThread::ShutdownComplete\n"

         // Bug 1600572
         "race:SchedulerGroup::CreateEventTargetFor\n"
         "race:SystemGroupImpl::AddRef\n"
         "race:SystemGroup::EventTargetFor\n"
         "race:SchedulerEventTarget::AddRef\n"
         "race:SchedulerEventTarget::Dispatch\n"
         "race:ContentChild::GetSpecificMessageEventTarget\n"
         "race:MessageChannel::MessageTask::Post\n"

         // Bug 1600594
         "race:nsThread::SizeOfEventQueues\n"

         // Bug 1600895
         "race:js::gc::MovingTracer::onBaseShapeEdge\n"
         "race:js::gc::MovingTracer::onScopeEdge\n"
         "race:js::gc::MovingTracer::onShapeEdge\n"

         // Bug 1601286
         "race:setFlagBit\n"
         "race:isFatInline\n"
         "race:AtomizeAndCopyCharsFromLookup\n"
         "race:inlinedMarkAtomInternal\n"
         "race:XDRInnerObject<js::XDR_DECODE>\n"
         "race:ScriptStencil::finishGCThings\n"
         "race:XDRScriptGCThing<js::XDR_DECODE>\n"

         // Bug 1601600
         "race:SkARGB32_Blitter\n"
         "race:SkARGB32_Shader_Blitter\n"
         "race:SkARGB32_Opaque_Blitter\n"
         "race:SkRasterPipelineBlitter\n"
         "race:Clamp_S32_D32_nofilter_trans_shaderproc\n"

         // Bug 1601632
         "race:ScriptPreloader::MaybeFinishOffThreadDecode\n"
         "race:ScriptPreloader::DoFinishOffThreadDecode\n"

         // Bug 1601940
         "race:ApplyAsyncTestAttributes\n"
         "race:UnapplyAsyncTestAttributes\n"
         "race:MarkAsyncTransformAppliedToContent\n"

         // Bug 1601980
         "race:image::RasterImage::StartDecoding\n"
         "race:image::RasterImage::OnImageDataAvailable\n"

         // Bug 1603504
         "race:HttpChannelParent::OnDataAvailable\n"

         // Bug 1606635
         "race:DatabaseWork\n"
         "race:OpenDirectory\n"

         // Bug 1606647
         "race:nsSocketTransport::OnSocketReady\n"

         // Bug 1606651
         "race:nsPluginTag::nsPluginTag\n"
         "race:nsFakePluginTag\n"

         // Bug 1606800
         "race:CallInitFunc\n"

         // Bug 1606803
         "race:ipv6_is_present\n"

         // Bug 1606804
         "deadlock:third_party/rust/rkv/src/env.rs\n"

         // Bug 1606860
         "race:majorGCCount\n"
         "race:incMajorGcNumber\n"

         // Bug 1606864
         "race:nsSocketTransport::Close\n"
         "race:nsSocketTransport::OnSocketDetached\n"

         // Bug 1607212
         "race:CacheEntry::InvokeCallback\n"

         // Bug 1607218
         "race:nsProxyInfo::SetResolveFlags\n"
         "race:nsProxyInfo::GetResolveFlags\n"

         // Bug 1615014
         "race:EnsurePerformanceCounter\n"
         "race:GetPerformanceCounter\n"

         // Bug 1607134
         "race:net::sRunningIndex\n"

         // Bug 1607138
         "race:gXPCOMThreadsShutDown\n"

         // Bug 1607426
         "race:PACLoadComplete::Run\n"
         "race:nsPACMan::ProcessPending\n"

         // Bug 1607221
         "race:nsSocketTransport::SetTimeout\n"

         // Bug 1607446
         "race:nsJARChannel::Suspend\n"
         "race:nsJARChannel::Resume\n"

         // Bug 1607449
         "race:fill_CERTCertificateFields\n"
         "race:CERT_DestroyCertificate\n"

         // Bug 1607588
         "race:nssSlot_GetToken\n"
         "race:nssToken_Destroy\n"

         // Bug 1607704
         "race:nsUrlClassifierDBServiceWorker::OpenDb\n"
         "race:nsUrlClassifierDBServiceWorker::Shutdown\n"

         // Bug 1607706
         "race:TemporaryIPCBlobParent::CreateAndShareFile\n"

         // Bug 1607712
         "race:GtkCompositorWidget::NotifyClientSizeChanged\n"
         "race:GtkCompositorWidget::GetClientSize\n"

         // Bug 1607762
         "race:nsHtml5OwningUTF16Buffer::Release\n"

         // Bug 1608357
         "race:nsHtml5ExecutorFlusher::Run\n"
         "race:geckoservo::glue::traverse_subtree\n"

         // Bug 1608462
         "deadlock:ScriptPreloader::OffThreadDecodeCallback\n"

         // Bug 1612054
         "race:nsContentSecurityUtils::IsEvalAllowed\n"
         "race:nsContentSecurityUtils::ValidateScriptFilename\n"

         // Bug 1613384
         "race:GCRuntime::setPerformanceHint\n"
         "race:GCHeapThreshold::updateAfterGC\n"

         // Bug 1614605
         "deadlock:SanctionsTestServer\n"
         "deadlock:OCSPStaplingServer\n"

         // Bug 1614646
         "race:nsCookieService::CountCookiesFromHostInternal\n"
         "race:nsCookieService::InitDBStates\n"

         // Bug 1614706
         "race:CacheFileInputStream::Release\n"
         "race:CacheFileInputStream::CloseWithStatus\n"

         // Bug 1615017
         "race:CacheFileMetadata::SetHash\n"
         "race:CacheFileMetadata::OnDataWritten\n"

         // Bug 1615123
         "race:_dl_deallocate_tls\n"
         "race:__libc_memalign\n"

         // Bug 1615121
         "race:CacheEntry::Purge\n"
         "race:CacheEntry::MetaDataReady\n"

         // Bug 1615275
         "race:^GetVelocity$\n"
         "race:^EndTouch$\n"

         // Bug 1615265
         "race:ScriptPreloader::OffThreadDecodeCallback\n"

         // ~GLContextGLX unlocks a libGL mutex that cannot be seen
         // by TSan because libGL is not instrumented.
         "mutex:GLContextGLX::~GLContextGLX\n"

         // Probably false positives in Rust code
         "race:third_party/rust/parking_lot_core/*\n"

         // Rust library is not instrumented
         "race:/rustc/*.rs\n"
         "deadlock:/rustc/*.rs\n"
         "thread:std::sys::unix::thread::Thread::new\n"

         // Logging bug in Mochitests
         "race:mochitest/ssltunnel/ssltunnel.cpp\n"

         // Suppress thread leaks for now
         "thread:NS_NewNamedThread\n"
         "thread:nsThread::Init\n"
         "thread:libglib-2\n"

         // This thread does not seem to be stopped/joined
         "thread:mozilla::layers::ImageBridgeChild\n"
         "race:mozilla::layers::ImageBridgeChild::ShutDown\n"

         // Benign races in third-party code
         //
         // SIMD Initialization in libjpeg, potentially runs
         // initialization twice, but otherwise benign. Init
         // routine itself is in native assembler.
         "race:init_simd\n"
         "race:simd_support\n"
         "race:jsimd_can_ycc_rgb\n"
         // Likely benign race in ipc/chromium/ where we set
         // `message_loop_` to `NULL` on two threads when stopping
         // a thread at the same time it is already finishing.
         // See also bug 1615228 for discussion.
         "race:base::Thread::Stop\n"

      // End of suppressions.
      ;  // Please keep this semicolon.
}
#endif  // _MSC_VER
