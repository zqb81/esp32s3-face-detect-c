const { buildFaceImageUrl, getFaceImages } = require('../../utils/api');
const {
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

function normalizeImages(faceImages) {
  return (faceImages || []).map((item) => ({
    id: item.id,
    datetime: formatDateTime(item.datetime),
    device: formatText(item.device, 'unknown'),
    imageError: false,
    imageUrl: buildFaceImageUrl(item.id),
    scoreText: formatScore(item.score)
  }));
}

Page({
  data: {
    errorMessage: '',
    hasError: false,
    images: [],
    lastRefreshText: '尚未刷新',
    loading: true,
    pageKey: 'faces'
  },

  onLoad() {
    this.loadImages(true);
  },

  onPullDownRefresh() {
    this.loadImages(false, true);
  },

  async loadImages(showLoading, fromPullDown) {
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
      const images = await getFaceImages(20);
      this.setData({
        errorMessage: '',
        hasError: false,
        images: normalizeImages(images),
        lastRefreshText: formatRefreshTime(Date.now()),
        loading: false
      });
    } catch (error) {
      this.setData({
        errorMessage: '抓拍图片加载失败，请检查 `/api/face_images` 与 `/api/face_image/<id>` 是否正常。',
        hasError: true,
        images: [],
        loading: false
      });
    } finally {
      this._loading = false;

      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
    }
  },

  handleManualRefresh() {
    this.loadImages(false);
  },

  handleImageError(event) {
    const faceId = event.currentTarget.dataset.id;
    const images = this.data.images.map((item) => {
      if (String(item.id) === String(faceId)) {
        return Object.assign({}, item, {
          imageError: true
        });
      }
      return item;
    });

    this.setData({
      images
    });
  },

  previewImage(event) {
    const currentUrl = event.currentTarget.dataset.url;
    const urls = this.data.images
      .filter((item) => !item.imageError)
      .map((item) => item.imageUrl);

    if (!currentUrl || !urls.length) {
      return;
    }

    wx.previewImage({
      current: currentUrl,
      urls
    });
  },

  goHome() {
    this.relaunchTo(PAGE_PATHS.home);
  },

  goDetections() {
    this.relaunchTo(PAGE_PATHS.detections);
  },

  goFaces() {
    return;
  },

  relaunchTo(url) {
    if (url === PAGE_PATHS.faces) {
      return;
    }

    wx.reLaunch({
      url
    });
  }
});
