import readline from 'readline';
import MarkovGenerator from './src/MarkovGenerator';

const bot = new Bot('slm');

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
});

console.log('You can start chatting with the bot. Type your message and press Enter.');
rl.prompt();

rl.on('line', async (input) => {
  try {
    if (input.trim() === '') {
      console.log('Please enter a message.');
      rl.prompt();
      return;
    }

    const response = await bot.generateReply(input);
    if (response) {
      console.log('\nBot: ' + response + '\n');
    }
  } catch (error) {
    console.error('Error:', error.message);
  }

  rl.prompt();
});