function pad2(value) {
  return String(value).padStart(2, '0');
}

function formatDateTime(value) {
  if (!value || value === 'N/A') {
    return '暂无';
  }

  if (typeof value === 'string') {
    return value;
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return '暂无';
  }

  return [
    date.getFullYear(),
    pad2(date.getMonth() + 1),
    pad2(date.getDate())
  ].join('-') + ' ' + [
    pad2(date.getHours()),
    pad2(date.getMinutes()),
    pad2(date.getSeconds())
  ].join(':');
}

function formatRefreshTime(value) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return '尚未刷新';
  }

  return [
    pad2(date.getHours()),
    pad2(date.getMinutes()),
    pad2(date.getSeconds())
  ].join(':');
}

function formatScore(value) {
  const score = Number(value);
  if (Number.isNaN(score)) {
    return '--';
  }
  return score.toFixed(2);
}

function formatBox(box) {
  if (!Array.isArray(box) || box.length < 4) {
    return '坐标缺失';
  }

  return `(${box[0]}, ${box[1]}) - (${box[2]}, ${box[3]})`;
}

function formatText(value, fallback) {
  if (value === undefined || value === null || value === '') {
    return fallback || '--';
  }
  return String(value);
}

module.exports = {
  formatBox,
  formatDateTime,
  formatRefreshTime,
  formatScore,
  formatText
};
