module.exports = {
  apps: [{
    name: 'markov-api',
    script: './server.js',
    autorestart: true,
    max_memory_restart: '1G',
    merge_logs: true,
    env: {
      NODE_ENV: 'production',
      PORT: 3001,
    },
  }]
};
