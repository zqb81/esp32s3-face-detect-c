const { getDetections } = require('../../utils/api');
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
  home: '/pages/home/home'
};

function normalizeRecords(records) {
  return (records || []).map((record) => ({
    id: record.id,
    datetime: formatDateTime(record.datetime),
    device: formatText(record.device, 'unknown'),
    faceCount: record.face_count !== undefined ? record.face_count : 0,
    frame: formatText(record.frame, '--'),
    faces: (record.faces || []).map((face, index) => ({
      id: `${record.id}-${index}`,
      scoreText: formatScore(face.score),
      boxText: formatBox(face.box)
    }))
  }));
}

Page({
  data: {
    errorMessage: '',
    hasError: false,
    lastRefreshText: '尚未刷新',
    loading: true,
    pageKey: 'detections',
    records: []
  },

  onLoad() {
    this.loadRecords(true);
  },

  onPullDownRefresh() {
    this.loadRecords(false, true);
  },

  async loadRecords(showLoading, fromPullDown) {
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

    try {
      const records = await getDetections(20);
      this.setData({
        errorMessage: '',
        hasError: false,
        lastRefreshText: formatRefreshTime(Date.now()),
        loading: false,
        records: normalizeRecords(records)
      });
    } catch (error) {
      this.setData({
        errorMessage: '检测记录加载失败，请确认服务已启动且接口可访问。',
        hasError: true,
        loading: false,
        records: []
      });
    } finally {
      this._loading = false;

      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
    }
  },

  handleManualRefresh() {
    this.loadRecords(false);
  },

  goHome() {
    this.relaunchTo(PAGE_PATHS.home);
  },

  goDetections() {
    return;
  },

  goFaces() {
    this.relaunchTo(PAGE_PATHS.faces);
  },

  relaunchTo(url) {
    if (url === PAGE_PATHS.detections) {
      return;
    }

    wx.reLaunch({
      url
    });
  }
});
