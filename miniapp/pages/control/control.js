const { BASE_URL } = require('../../utils/api');

Page({
  data: {
    buzzerOn: false,
    faceOn: true,
    rgbColor: 'off',
    chatMsgs: [],
    chatInput: '',
    scrollId: '',
    msgId: 0
  },

  onLoad() {},

  request(path, method = 'GET', body = null) {
    return new Promise((resolve, reject) => {
      wx.request({
        url: BASE_URL + path,
        method,
        data: body,
        header: { 'Content-Type': 'application/json' },
        success: r => r.statusCode === 200 ? resolve(r) : reject(r),
        fail: reject
      });
    });
  },

  toggleBuzzer(e) {
    const on = e.detail.value;
    this.setData({ buzzerOn: on });
    this.sendDevice('buzzer', on ? 'on' : 'off');
  },

  toggleFace(e) {
    const on = e.detail.value;
    this.setData({ faceOn: on });
    this.sendDevice('face_detect', on ? 'on' : 'off');
  },

  setRgbColor(e) {
    const color = e.currentTarget.dataset.color;
    this.setData({ rgbColor: color });
    this.sendDevice('rgb', color);
  },

  async sendDevice(action, state) {
    try {
      await this.request('/api/device', 'POST', { action, state });
    } catch (e) {
      wx.showToast({ title: '控制失败', icon: 'none' });
    }
  },

  onChatInput(e) {
    this.setData({ chatInput: e.detail.value });
  },

  async sendChat() {
    const text = this.data.chatInput.trim();
    if (!text) return;

    const id = this.data.msgId + 1;
    const now = new Date().toLocaleTimeString();
    const chatMsgs = [...this.data.chatMsgs, { id, role: 'user', text, time: now }];
    this.setData({ chatMsgs, chatInput: '', msgId: id, scrollId: 'chat-bottom' });

    try {
      const res = await this.request('/api/voice_chat', 'POST', { text });
      const data = res.data || {};
      const id2 = this.data.msgId + 1;
      this.setData({
        chatMsgs: [...this.data.chatMsgs, { id: id2, role: 'ai', text: data.reply || '(无回复)', time: new Date().toLocaleTimeString() }],
        msgId: id2,
        scrollId: 'chat-bottom'
      });
      if (data.actions) {
        data.actions.forEach(a => {
          if (a.action === 'buzzer') this.setData({ buzzerOn: a.state === 'on' });
          if (a.action === 'face_detect') this.setData({ faceOn: a.state === 'on' });
          if (a.action === 'rgb') this.setData({ rgbColor: a.color || 'off' });
        });
      }
    } catch (e) {
      const id3 = this.data.msgId + 1;
      this.setData({
        chatMsgs: [...this.data.chatMsgs, { id: id3, role: 'ai', text: '请求失败', time: new Date().toLocaleTimeString() }],
        msgId: id3
      });
    }
  }
});
