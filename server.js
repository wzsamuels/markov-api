import Fastify from 'fastify';
import MarkovGenerator from './src/MarkovGenerator.js';

const fastify = Fastify({ logger: true });

const bots = {
  classic: new MarkovGenerator('classic'),
  modern: new MarkovGenerator('modern'),
  blended: new MarkovGenerator('blended')
}

fastify.post('/generate', async(request, reply) => {
  const { prompt, persona = 'blended' } = request.body;

  if (!prompt) {
    return reply.status(400).send({ error: 'Prompt is required' });
  }

  const bot = bots[persona] || bots['blended'];

  try {
    const reponseText = bot.generateReply(prompt);

    return {
      prompt: prompt,
      persona: persona,
      response: reponseText
    };
  } catch (error) {
    fastify.log.error(error);
    return reply.status(500).send({ error: 'Failed to generate response' });
  }
});

const start = async () => {
  try {
    await fastify.listen({ port:5001, host: '127.0.0.1' });
    console.log(`Server listening on http://127.0.0.1:5001`);
  } catch (error) {
    fastify.log.error(error);
    process.exit(1);
  }
};

start();
