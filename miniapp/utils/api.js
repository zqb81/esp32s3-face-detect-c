const { BASE_URL } = require('./config');


function request(options) {
  const {
    url,
    method = 'GET',
    data,
    timeout = 5000
  } = options;

  return new Promise((resolve, reject) => {
    wx.request({
      url: `${BASE_URL}${url}`,
      method,
      data,
      timeout,
      header: {
        'content-type': 'application/json'
      },
      success(res) {
        if (res.statusCode >= 200 && res.statusCode < 300) {
          resolve(res.data);
          return;
        }

        reject(new Error(`接口请求失败：${res.statusCode}`));
      },
      fail(err) {
        reject(err);
      }
    });
  });
}

function getStats() {
  return request({
    url: '/api/stats'
  });
}

function getLatestDetection() {
  return request({
    url: '/api/latest_detection'
  });
}

function getDetections(limit) {
  return request({
    url: '/api/detections',
    data: {
      limit: limit || 20
    }
  });
}

function getFaceImages(limit) {
  return request({
    url: '/api/face_images',
    data: {
      limit: limit || 20
    }
  });
}

function buildFaceImageUrl(faceId) {
  return `${BASE_URL}/api/face_image/${faceId}`;
}

module.exports = {
  BASE_URL,
  buildFaceImageUrl,
  getDetections,
  getFaceImages,
  getLatestDetection,
  getStats,
  request
};
