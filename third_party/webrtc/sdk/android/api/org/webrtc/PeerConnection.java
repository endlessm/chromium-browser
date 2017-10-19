/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import java.util.Collections;
import java.util.LinkedList;
import java.util.List;

/**
 * Java-land version of the PeerConnection APIs; wraps the C++ API
 * http://www.webrtc.org/reference/native-apis, which in turn is inspired by the
 * JS APIs: http://dev.w3.org/2011/webrtc/editor/webrtc.html and
 * http://www.w3.org/TR/mediacapture-streams/
 */
public class PeerConnection {
  static {
    System.loadLibrary("jingle_peerconnection_so");
  }

  /** Tracks PeerConnectionInterface::IceGatheringState */
  public enum IceGatheringState { NEW, GATHERING, COMPLETE }

  /** Tracks PeerConnectionInterface::IceConnectionState */
  public enum IceConnectionState {
    NEW,
    CHECKING,
    CONNECTED,
    COMPLETED,
    FAILED,
    DISCONNECTED,
    CLOSED
  }

  /** Tracks PeerConnectionInterface::TlsCertPolicy */
  public enum TlsCertPolicy {
    TLS_CERT_POLICY_SECURE,
    TLS_CERT_POLICY_INSECURE_NO_CHECK,
  }

  /** Tracks PeerConnectionInterface::SignalingState */
  public enum SignalingState {
    STABLE,
    HAVE_LOCAL_OFFER,
    HAVE_LOCAL_PRANSWER,
    HAVE_REMOTE_OFFER,
    HAVE_REMOTE_PRANSWER,
    CLOSED
  }

  /** Java version of PeerConnectionObserver. */
  public static interface Observer {
    /** Triggered when the SignalingState changes. */
    public void onSignalingChange(SignalingState newState);

    /** Triggered when the IceConnectionState changes. */
    public void onIceConnectionChange(IceConnectionState newState);

    /** Triggered when the ICE connection receiving status changes. */
    public void onIceConnectionReceivingChange(boolean receiving);

    /** Triggered when the IceGatheringState changes. */
    public void onIceGatheringChange(IceGatheringState newState);

    /** Triggered when a new ICE candidate has been found. */
    public void onIceCandidate(IceCandidate candidate);

    /** Triggered when some ICE candidates have been removed. */
    public void onIceCandidatesRemoved(IceCandidate[] candidates);

    /** Triggered when media is received on a new stream from remote peer. */
    public void onAddStream(MediaStream stream);

    /** Triggered when a remote peer close a stream. */
    public void onRemoveStream(MediaStream stream);

    /** Triggered when a remote peer opens a DataChannel. */
    public void onDataChannel(DataChannel dataChannel);

    /** Triggered when renegotiation is necessary. */
    public void onRenegotiationNeeded();

    /**
     * Triggered when a new track is signaled by the remote peer, as a result of
     * setRemoteDescription.
     */
    public void onAddTrack(RtpReceiver receiver, MediaStream[] mediaStreams);
  }

  /** Java version of PeerConnectionInterface.IceServer. */
  public static class IceServer {
    // List of URIs associated with this server. Valid formats are described
    // in RFC7064 and RFC7065, and more may be added in the future. The "host"
    // part of the URI may contain either an IP address or a hostname.
    public final String uri;
    public final String username;
    public final String password;
    public final TlsCertPolicy tlsCertPolicy;

    // If the URIs in |urls| only contain IP addresses, this field can be used
    // to indicate the hostname, which may be necessary for TLS (using the SNI
    // extension). If |urls| itself contains the hostname, this isn't
    // necessary.
    public final String hostname;

    // List of protocols to be used in the TLS ALPN extension.
    public final List<String> tlsAlpnProtocols;

    /** Convenience constructor for STUN servers. */
    public IceServer(String uri) {
      this(uri, "", "");
    }

    public IceServer(String uri, String username, String password) {
      this(uri, username, password, TlsCertPolicy.TLS_CERT_POLICY_SECURE);
    }

    public IceServer(String uri, String username, String password, TlsCertPolicy tlsCertPolicy) {
      this(uri, username, password, tlsCertPolicy, "");
    }

    public IceServer(String uri, String username, String password, TlsCertPolicy tlsCertPolicy,
        String hostname) {
      this(uri, username, password, tlsCertPolicy, hostname, null);
    }

    private IceServer(String uri, String username, String password, TlsCertPolicy tlsCertPolicy,
        String hostname, List<String> tlsAlpnProtocols) {
      this.uri = uri;
      this.username = username;
      this.password = password;
      this.tlsCertPolicy = tlsCertPolicy;
      this.hostname = hostname;
      this.tlsAlpnProtocols = tlsAlpnProtocols;
    }

    public String toString() {
      return uri + " [" + username + ":" + password + "] [" + tlsCertPolicy + "] [" + hostname
          + "] [" + tlsAlpnProtocols + "]";
    }

    public static Builder builder(String uri) {
      return new Builder(uri);
    }

    public static class Builder {
      private String uri;
      private String username = "";
      private String password = "";
      private TlsCertPolicy tlsCertPolicy = TlsCertPolicy.TLS_CERT_POLICY_SECURE;
      private String hostname = "";
      private List<String> tlsAlpnProtocols;

      private Builder(String uri) {
        this.uri = uri;
      }

      public Builder setUsername(String username) {
        this.username = username;
        return this;
      }

      public Builder setPassword(String password) {
        this.password = password;
        return this;
      }

      public Builder setTlsCertPolicy(TlsCertPolicy tlsCertPolicy) {
        this.tlsCertPolicy = tlsCertPolicy;
        return this;
      }

      public Builder setHostname(String hostname) {
        this.hostname = hostname;
        return this;
      }

      public Builder setTlsAlpnProtocols(List<String> tlsAlpnProtocols) {
        this.tlsAlpnProtocols = tlsAlpnProtocols;
        return this;
      }

      public IceServer createIceServer() {
        return new IceServer(uri, username, password, tlsCertPolicy, hostname, tlsAlpnProtocols);
      }
    }
  }

  /** Java version of PeerConnectionInterface.IceTransportsType */
  public enum IceTransportsType { NONE, RELAY, NOHOST, ALL }

  /** Java version of PeerConnectionInterface.BundlePolicy */
  public enum BundlePolicy { BALANCED, MAXBUNDLE, MAXCOMPAT }

  /** Java version of PeerConnectionInterface.RtcpMuxPolicy */
  public enum RtcpMuxPolicy { NEGOTIATE, REQUIRE }

  /** Java version of PeerConnectionInterface.TcpCandidatePolicy */
  public enum TcpCandidatePolicy { ENABLED, DISABLED }

  /** Java version of PeerConnectionInterface.CandidateNetworkPolicy */
  public enum CandidateNetworkPolicy { ALL, LOW_COST }

  /** Java version of rtc::KeyType */
  public enum KeyType { RSA, ECDSA }

  /** Java version of PeerConnectionInterface.ContinualGatheringPolicy */
  public enum ContinualGatheringPolicy { GATHER_ONCE, GATHER_CONTINUALLY }

  /** Java version of rtc::IntervalRange */
  public static class IntervalRange {
    private final int min;
    private final int max;

    public IntervalRange(int min, int max) {
      this.min = min;
      this.max = max;
    }

    public int getMin() {
      return min;
    }

    public int getMax() {
      return max;
    }
  }

  /** Java version of PeerConnectionInterface.RTCConfiguration */
  public static class RTCConfiguration {
    public IceTransportsType iceTransportsType;
    public List<IceServer> iceServers;
    public BundlePolicy bundlePolicy;
    public RtcpMuxPolicy rtcpMuxPolicy;
    public TcpCandidatePolicy tcpCandidatePolicy;
    public CandidateNetworkPolicy candidateNetworkPolicy;
    public int audioJitterBufferMaxPackets;
    public boolean audioJitterBufferFastAccelerate;
    public int iceConnectionReceivingTimeout;
    public int iceBackupCandidatePairPingInterval;
    public KeyType keyType;
    public ContinualGatheringPolicy continualGatheringPolicy;
    public int iceCandidatePoolSize;
    public boolean pruneTurnPorts;
    public boolean presumeWritableWhenFullyRelayed;
    public Integer iceCheckMinInterval;
    public boolean disableIPv6OnWifi;
    // By default, PeerConnection will use a limited number of IPv6 network
    // interfaces, in order to avoid too many ICE candidate pairs being created
    // and delaying ICE completion.
    //
    // Can be set to Integer.MAX_VALUE to effectively disable the limit.
    public int maxIPv6Networks;
    public IntervalRange iceRegatherIntervalRange;

    // TODO(deadbeef): Instead of duplicating the defaults here, we should do
    // something to pick up the defaults from C++. The Objective-C equivalent
    // of RTCConfiguration does that.
    public RTCConfiguration(List<IceServer> iceServers) {
      iceTransportsType = IceTransportsType.ALL;
      bundlePolicy = BundlePolicy.BALANCED;
      rtcpMuxPolicy = RtcpMuxPolicy.REQUIRE;
      tcpCandidatePolicy = TcpCandidatePolicy.ENABLED;
      candidateNetworkPolicy = candidateNetworkPolicy.ALL;
      this.iceServers = iceServers;
      audioJitterBufferMaxPackets = 50;
      audioJitterBufferFastAccelerate = false;
      iceConnectionReceivingTimeout = -1;
      iceBackupCandidatePairPingInterval = -1;
      keyType = KeyType.ECDSA;
      continualGatheringPolicy = ContinualGatheringPolicy.GATHER_ONCE;
      iceCandidatePoolSize = 0;
      pruneTurnPorts = false;
      presumeWritableWhenFullyRelayed = false;
      iceCheckMinInterval = null;
      disableIPv6OnWifi = false;
      maxIPv6Networks = 5;
      iceRegatherIntervalRange = null;
    }
  };

  private final List<MediaStream> localStreams;
  private final long nativePeerConnection;
  private final long nativeObserver;
  private List<RtpSender> senders;
  private List<RtpReceiver> receivers;

  PeerConnection(long nativePeerConnection, long nativeObserver) {
    this.nativePeerConnection = nativePeerConnection;
    this.nativeObserver = nativeObserver;
    localStreams = new LinkedList<MediaStream>();
    senders = new LinkedList<RtpSender>();
    receivers = new LinkedList<RtpReceiver>();
  }

  // JsepInterface.
  public native SessionDescription getLocalDescription();

  public native SessionDescription getRemoteDescription();

  public native DataChannel createDataChannel(String label, DataChannel.Init init);

  public native void createOffer(SdpObserver observer, MediaConstraints constraints);

  public native void createAnswer(SdpObserver observer, MediaConstraints constraints);

  public native void setLocalDescription(SdpObserver observer, SessionDescription sdp);

  public native void setRemoteDescription(SdpObserver observer, SessionDescription sdp);

  public boolean setConfiguration(RTCConfiguration config) {
    return nativeSetConfiguration(config, nativeObserver);
  }

  public boolean addIceCandidate(IceCandidate candidate) {
    return nativeAddIceCandidate(candidate.sdpMid, candidate.sdpMLineIndex, candidate.sdp);
  }

  public boolean removeIceCandidates(final IceCandidate[] candidates) {
    return nativeRemoveIceCandidates(candidates);
  }

  public boolean addStream(MediaStream stream) {
    boolean ret = nativeAddLocalStream(stream.nativeStream);
    if (!ret) {
      return false;
    }
    localStreams.add(stream);
    return true;
  }

  public void removeStream(MediaStream stream) {
    nativeRemoveLocalStream(stream.nativeStream);
    localStreams.remove(stream);
  }

  /**
   * Creates an RtpSender without a track.
   * <p>
   * This method allows an application to cause the PeerConnection to negotiate
   * sending/receiving a specific media type, but without having a track to
   * send yet.
   * <p>
   * When the application does want to begin sending a track, it can call
   * RtpSender.setTrack, which doesn't require any additional SDP negotiation.
   * <p>
   * Example use:
   * <pre>
   * {@code
   * audioSender = pc.createSender("audio", "stream1");
   * videoSender = pc.createSender("video", "stream1");
   * // Do normal SDP offer/answer, which will kick off ICE/DTLS and negotiate
   * // media parameters....
   * // Later, when the endpoint is ready to actually begin sending:
   * audioSender.setTrack(audioTrack, false);
   * videoSender.setTrack(videoTrack, false);
   * }
   * </pre>
   * Note: This corresponds most closely to "addTransceiver" in the official
   * WebRTC API, in that it creates a sender without a track. It was
   * implemented before addTransceiver because it provides useful
   * functionality, and properly implementing transceivers would have required
   * a great deal more work.
   *
   * @param kind      Corresponds to MediaStreamTrack kinds (must be "audio" or
   *                  "video").
   * @param stream_id The ID of the MediaStream that this sender's track will
   *                  be associated with when SDP is applied to the remote
   *                  PeerConnection. If createSender is used to create an
   *                  audio and video sender that should be synchronized, they
   *                  should use the same stream ID.
   * @return          A new RtpSender object if successful, or null otherwise.
   */
  public RtpSender createSender(String kind, String stream_id) {
    RtpSender new_sender = nativeCreateSender(kind, stream_id);
    if (new_sender != null) {
      senders.add(new_sender);
    }
    return new_sender;
  }

  // Note that calling getSenders will dispose of the senders previously
  // returned (and same goes for getReceivers).
  public List<RtpSender> getSenders() {
    for (RtpSender sender : senders) {
      sender.dispose();
    }
    senders = nativeGetSenders();
    return Collections.unmodifiableList(senders);
  }

  public List<RtpReceiver> getReceivers() {
    for (RtpReceiver receiver : receivers) {
      receiver.dispose();
    }
    receivers = nativeGetReceivers();
    return Collections.unmodifiableList(receivers);
  }

  // Older, non-standard implementation of getStats.
  @Deprecated
  public boolean getStats(StatsObserver observer, MediaStreamTrack track) {
    return nativeOldGetStats(observer, (track == null) ? 0 : track.nativeTrack);
  }

  // Gets stats using the new stats collection API, see webrtc/api/stats/. These
  // will replace old stats collection API when the new API has matured enough.
  public void getStats(RTCStatsCollectorCallback callback) {
    nativeNewGetStats(callback);
  }

  // Limits the bandwidth allocated for all RTP streams sent by this
  // PeerConnection. Pass null to leave a value unchanged.
  public native boolean setBitrate(Integer min, Integer current, Integer max);

  // Starts recording an RTC event log. Ownership of the file is transfered to
  // the native code. If an RTC event log is already being recorded, it will be
  // stopped and a new one will start using the provided file. Logging will
  // continue until the stopRtcEventLog function is called. The max_size_bytes
  // argument is ignored, it is added for future use.
  public boolean startRtcEventLog(int file_descriptor, int max_size_bytes) {
    return nativeStartRtcEventLog(file_descriptor, max_size_bytes);
  }

  // Stops recording an RTC event log. If no RTC event log is currently being
  // recorded, this call will have no effect.
  public void stopRtcEventLog() {
    nativeStopRtcEventLog();
  }

  // TODO(fischman): add support for DTMF-related methods once that API
  // stabilizes.
  public native SignalingState signalingState();

  public native IceConnectionState iceConnectionState();

  public native IceGatheringState iceGatheringState();

  public native void close();

  public void dispose() {
    close();
    for (MediaStream stream : localStreams) {
      nativeRemoveLocalStream(stream.nativeStream);
      stream.dispose();
    }
    localStreams.clear();
    for (RtpSender sender : senders) {
      sender.dispose();
    }
    senders.clear();
    for (RtpReceiver receiver : receivers) {
      receiver.dispose();
    }
    receivers.clear();
    JniCommon.nativeReleaseRef(nativePeerConnection);
    freeObserver(nativeObserver);
  }

  private static native void freeObserver(long nativeObserver);

  public native boolean nativeSetConfiguration(RTCConfiguration config, long nativeObserver);

  private native boolean nativeAddIceCandidate(
      String sdpMid, int sdpMLineIndex, String iceCandidateSdp);

  private native boolean nativeRemoveIceCandidates(final IceCandidate[] candidates);

  private native boolean nativeAddLocalStream(long nativeStream);

  private native void nativeRemoveLocalStream(long nativeStream);

  private native boolean nativeOldGetStats(StatsObserver observer, long nativeTrack);

  private native void nativeNewGetStats(RTCStatsCollectorCallback callback);

  private native RtpSender nativeCreateSender(String kind, String stream_id);

  private native List<RtpSender> nativeGetSenders();

  private native List<RtpReceiver> nativeGetReceivers();

  private native boolean nativeStartRtcEventLog(int file_descriptor, int max_size_bytes);

  private native void nativeStopRtcEventLog();
}
