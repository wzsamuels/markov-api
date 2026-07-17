import Fastify from 'fastify';
import MarkovGenerator from './MarkovGenerator.js';

const fastify = Fastify({ logger: true });

const bots = {
  classic: new MarkovGenerator('classic'),
  modern: new MarkovGenerator('modern'),
  blended: new MarkovGenerator('blended')
}

fastify.post('/api/chat', async(request, reply) => {
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
    await fastify.listen({ port:3001, host: '0.0.0.0' });
    console.log(`Server listening on http://localhost:3001`);
  } catch (error) {
    fastify.log.error(error);
    process.exit(1);
  }
};

start();
