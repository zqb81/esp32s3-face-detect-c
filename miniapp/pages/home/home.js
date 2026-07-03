const {
  BASE_URL,
  buildFaceImageUrl,
  getFaceImages,
  getLatestDetection,
  getStats
} = require('../../utils/api');
const {
  formatBox,
  formatDateTime,
  formatRefreshTime,
  formatScore,
  formatText
} = require('../../utils/format');

const PAGE_PATHS = {
  detections: '/pages/detections/detections',
  faces: '/pages/faces/faces',
  control: '/pages/control/control',
  home: '/pages/home/home'
};

function buildStatsCards(stats) {
  return [
    {
      label: '总检测数',
      value: formatText(stats.total_detections, '0'),
      accentClass: 'stat-accent-cyan'
    },
    {
      label: '总人脸数',
      value: formatText(stats.total_faces, '0'),
      accentClass: 'stat-accent-blue'
    },
    {
      label: '在线设备数',
      value: formatText(stats.devices_online, '0'),
      accentClass: 'stat-accent-gold'
    },
    {
      label: '最近检测时间',
      value: formatDateTime(stats.last_detection),
      accentClass: 'stat-accent-rose'
    }
  ];
}

function normalizeDetection(detection) {
  if (!detection || (!detection.datetime && !detection.device && !detection.face_count)) {
    return null;
  }

  const faces = (detection.faces || []).map((face, index) => ({
    id: `${index}-${formatScore(face.score)}`,
    scoreText: formatScore(face.score),
    boxText: formatBox(face.box)
  }));

  return {
    device: formatText(detection.device, 'unknown'),
    datetime: formatDateTime(detection.datetime),
    faceCount: detection.face_count !== undefined ? detection.face_count : faces.length,
    frame: formatText(detection.frame, '--'),
    faces
  };
}

function normalizeFaceImages(faceImages) {
  return (faceImages || []).slice(0, 6).map((item) => ({
    id: item.id,
    device: formatText(item.device, 'unknown'),
    datetime: formatDateTime(item.datetime),
    scoreText: formatScore(item.score),
    imageError: false,
    imageUrl: buildFaceImageUrl(item.id)
  }));
}

Page({
  data: {
    baseUrl: BASE_URL,
    connected: false,
    errorMessage: '',
    hasError: false,
    lastRefreshText: '尚未刷新',
    latestDetection: null,
    latestFaces: [],
    loading: true,
    pageKey: 'home',
    statsCards: buildStatsCards({})
  },

  onLoad() {
    this.loadDashboard(true);
  },

  onShow() {
    this.startAutoRefresh();
  },

  onHide() {
    this.stopAutoRefresh();
  },

  onUnload() {
    this.stopAutoRefresh();
  },

  onPullDownRefresh() {
    this.loadDashboard(false, true);
  },

  async loadDashboard(showLoading, fromPullDown) {
    if (this._loading) {
      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
      return;
    }

    this._loading = true;

    if (showLoading) {
      this.setData({
        loading: true
      });
    }

    const statsTask = getStats()
      .then((data) => ({ ok: true, data }))
      .catch((error) => ({ ok: false, error }));
    const detectionTask = getLatestDetection()
      .then((data) => ({ ok: true, data }))
      .catch((error) => ({ ok: false, error }));
    const facesTask = getFaceImages(6)
      .then((data) => ({ ok: true, data }))
      .catch((error) => ({ ok: false, error }));

    try {
      const [statsResult, detectionResult, facesResult] = await Promise.all([
        statsTask,
        detectionTask,
        facesTask
      ]);

      const connected = statsResult.ok || detectionResult.ok || facesResult.ok;

      this.setData({
        connected,
        errorMessage: connected ? '' : '当前无法连接到后端服务，请确认 `web/app.py` 已启动且 `BASE_URL` 配置正确。',
        hasError: !connected,
        lastRefreshText: formatRefreshTime(Date.now()),
        latestDetection: detectionResult.ok ? normalizeDetection(detectionResult.data) : null,
        latestFaces: facesResult.ok ? normalizeFaceImages(facesResult.data) : [],
        loading: false,
        statsCards: statsResult.ok ? buildStatsCards(statsResult.data) : buildStatsCards({})
      });
    } finally {
      this._loading = false;

      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
    }
  },

  handleManualRefresh() {
    this.loadDashboard(false);
  },

  handlePreviewImageError(event) {
    const faceId = event.currentTarget.dataset.id;
    const latestFaces = this.data.latestFaces.map((item) => {
      if (String(item.id) === String(faceId)) {
        return Object.assign({}, item, {
          imageError: true
        });
      }
      return item;
    });

    this.setData({
      latestFaces
    });
  },

  startAutoRefresh() {
    if (this._refreshTimer) {
      return;
    }

    this._refreshTimer = setInterval(() => {
      this.loadDashboard(false);
    }, 5000);
  },

  stopAutoRefresh() {
    if (!this._refreshTimer) {
      return;
    }

    clearInterval(this._refreshTimer);
    this._refreshTimer = null;
  },

  goHome() {
    this.relaunchTo(PAGE_PATHS.home);
  },

  goDetections() {
    this.relaunchTo(PAGE_PATHS.detections);
  },

  goFaces() {
    this.relaunchTo(PAGE_PATHS.faces);
  },

  goControl() {
    this.relaunchTo(PAGE_PATHS.control);
  },

  relaunchTo(url) {
    if (url === PAGE_PATHS.home) {
      return;
    }

    wx.reLaunch({
      url
    });
  }
});
