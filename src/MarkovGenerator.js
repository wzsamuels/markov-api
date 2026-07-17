// MarkovGenerator.js

import fs from 'fs'
import path from 'path'
import personas from './PersonaManager.js'

const ORDER = 3; // Default order for the Markov chain

class MarkovGenerator {
  #db

  constructor(personaName) {
    this.#db = personas.getDatabase(personaName); 
  }

  // Generate random text
  generate(maxTokens = 30) {
    // 1. Fast random selection bypassing full table scans and array building
    const getStart = this.#db.prepare(`
      SELECT state FROM n_grams 
      WHERE rowid >= (abs(random()) % (SELECT max(rowid) FROM n_grams))
        AND is_start = 1 
      LIMIT 1
    `);
    
    let row = getStart.get();

    // 2. Edge case fallback: If the random jump landed at the very end 
    // of the database and no start states remained, just grab the first one.
    if (!row) {
      row = this.#db.prepare(`SELECT state FROM n_grams WHERE is_start = 1 LIMIT 1`).get();
    }

    if (!row) {
      return ["I need to be trained with some text first!"];
    }

    let currentState = row.state;
    let result = currentState.split(' ');

    const getTransitions = this.#db.prepare(`SELECT next_token, frequency FROM transitions WHERE state = ?`);

    for (let i = ORDER; i < maxTokens; i++) {
      const possibleNextTokens = getTransitions.all(currentState);

      if (possibleNextTokens.length === 0) break;

      // Weighted random selection
      const totalWeight = possibleNextTokens.reduce((sum, r) => sum + r.frequency, 0);
      let randomNum = Math.floor(Math.random() * totalWeight);
      
      let nextToken = null;
      for (const r of possibleNextTokens) {
        randomNum -= r.frequency;
        if (randomNum < 0) {
          nextToken = r.next_token;
          break;
        }
      }

      result.push(nextToken);

      const stateArray = currentState.split(' ');
      stateArray.shift();
      stateArray.push(nextToken);
      currentState = stateArray.join(' ');

      if (['.', '?', '!'].includes(nextToken.slice(-1))) break;
    }

    return [result.join(' ')];
  }

  generateReply(userPrompt, maxTokens = 30) {
    // Quick check to ensure the database is populated
    const hasData = this.#db.prepare(`SELECT 1 FROM n_grams LIMIT 1`).get();
    if (!hasData) {
      return ["I need to be trained with some text first!"];
    }

    const promptTokens = userPrompt.trim().split(/\s+/);
    let seedState = null;

    // Prepare the search query once. 
    // ORDER BY RANDOM() LIMIT 1 instantly picks a random match at the database level.
    const findSeed = this.#db.prepare(`
      SELECT state FROM n_grams 
      WHERE state LIKE ? 
      LIMIT 50
    `);

    // Find the seed state
    for (let currentSize = ORDER; currentSize > 0; currentSize--) {
      if (seedState) break;

      for (let i = promptTokens.length - currentSize; i >= 0; i--) {
        const targetPhrase = promptTokens.slice(i, i + currentSize).join(' ');

        // The % wildcards allow the target phrase to be matched anywhere in the state
        const rows = findSeed.all(`${targetPhrase}%`);
        
        if (rows.length > 0) {
          // Pick a random seed from the fast results in Node, avoiding a DB sort.
          seedState = rows[Math.floor(Math.random() * rows.length)].state;
          break;
        }
      }
    }

    // Absolute Fallback if no match is found anywhere
    if (!seedState) {
      return this.generate(maxTokens);
    }

    // Prepare transition queries for both phases
    const getPrevTokens = this.#db.prepare(`SELECT prev_token, frequency FROM reverse_transitions WHERE state = ?`);
    const getNextTokens = this.#db.prepare(`SELECT next_token, frequency FROM transitions WHERE state = ?`);

    // --- PHASE 1: GENERATE BACKWARD ---
    let backwardTokens = [];
    let currentBackState = seedState;
    const maxBackTokens = Math.floor(maxTokens / 2);

    for (let i = 0; i < maxBackTokens; i++) {
      const possiblePrevTokens = getPrevTokens.all(currentBackState);
      if (possiblePrevTokens.length === 0) break;

      // Weighted random selection
      const totalWeight = possiblePrevTokens.reduce((sum, row) => sum + row.frequency, 0);
      let randomNum = Math.floor(Math.random() * totalWeight);
      
      let prevToken = null;
      for (const row of possiblePrevTokens) {
        randomNum -= row.frequency;
        if (randomNum < 0) {
          prevToken = row.prev_token;
          break;
        }
      }
      
      backwardTokens.unshift(prevToken);

      const stateArray = currentBackState.split(' ');
      stateArray.pop();
      stateArray.unshift(prevToken);
      currentBackState = stateArray.join(' ');

      if (['.', '?', '!'].includes(prevToken.slice(-1))) {
        backwardTokens.shift();
        break;
      }
    }

    // --- PHASE 2: GENERATE FORWARD ---
    let forwardTokens = [];
    let currentForwardState = seedState;
    const maxForwardTokens = maxTokens - backwardTokens.length - ORDER;

    for (let i = 0; i < maxForwardTokens; i++) {
      const possibleNextTokens = getNextTokens.all(currentForwardState);
      if (possibleNextTokens.length === 0) break;

      // Weighted random selection
      const totalWeight = possibleNextTokens.reduce((sum, row) => sum + row.frequency, 0);
      let randomNum = Math.floor(Math.random() * totalWeight);
      
      let nextToken = null;
      for (const row of possibleNextTokens) {
        randomNum -= row.frequency;
        if (randomNum < 0) {
          nextToken = row.next_token;
          break;
        }
      }
      
      forwardTokens.push(nextToken);

      const stateArray = currentForwardState.split(' ');
      stateArray.shift();
      stateArray.push(nextToken);
      currentForwardState = stateArray.join(' ');

      if (['.', '?', '!'].includes(nextToken.slice(-1))) {
         break;
      }
    }

    // --- PHASE 3: ASSEMBLE ---
    const seedArray = seedState.split(' ');
    const finalTokens = [...backwardTokens, ...seedArray, ...forwardTokens];

    let finalResponse = finalTokens.join(' ').trim();

    finalResponse = finalResponse.charAt(0).toUpperCase() + finalResponse.slice(1);
    if (!['.', '?', '!'].includes(finalResponse.slice(-1))) {
      finalResponse += '.';
    }

    return [finalResponse];
  }
}

export default MarkovGenerator
