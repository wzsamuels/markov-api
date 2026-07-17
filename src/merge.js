import Database from 'better-sqlite3';

console.log("Starting merge... This may take a few minutes depending on database size.");

const db = new Database('blended_markov.db');
db.exec(`ATTACH DATABASE 'modern_markov.db' AS soda;`);

const performMerge = db.transaction(() => {
  // 1. Merge N-Grams
  db.exec(`
    -- Update existing states if they are sentence starters in the new DB
    UPDATE n_grams 
    SET is_start = 1 
    WHERE state IN (SELECT state FROM soda.n_grams WHERE is_start = 1);
    
    -- Insert any brand new states
    INSERT OR IGNORE INTO n_grams (state, is_start) 
    SELECT state, is_start FROM soda.n_grams;
  `);

  // 2. Merge Forward Transitions
  db.exec(`
    -- Add the frequencies together where the transition exists in both databases
    UPDATE transitions 
    SET frequency = frequency + (
      SELECT frequency FROM soda.transitions s 
      WHERE s.state = transitions.state AND s.next_token = transitions.next_token
    )
    WHERE EXISTS (
      SELECT 1 FROM soda.transitions s 
      WHERE s.state = transitions.state AND s.next_token = transitions.next_token
    );
    
    -- Insert any brand new transitions
    INSERT OR IGNORE INTO transitions (state, next_token, frequency) 
    SELECT state, next_token, frequency FROM soda.transitions;
  `);

  // 3. Merge Reverse Transitions
  db.exec(`
    -- Add the frequencies together for reverse transitions
    UPDATE reverse_transitions 
    SET frequency = frequency + (
      SELECT frequency FROM soda.reverse_transitions s 
      WHERE s.state = reverse_transitions.state AND s.prev_token = reverse_transitions.prev_token
    )
    WHERE EXISTS (
      SELECT 1 FROM soda.reverse_transitions s 
      WHERE s.state = reverse_transitions.state AND s.prev_token = reverse_transitions.prev_token
    );
    
    -- Insert any brand new reverse transitions
    INSERT OR IGNORE INTO reverse_transitions (state, prev_token, frequency) 
    SELECT state, prev_token, frequency FROM soda.reverse_transitions;
  `);
});

// Execute the transaction
performMerge();

console.log("Data merged. Rebuilding indexes and optimizing file size...");

db.exec(`DETACH DATABASE soda;`);
db.exec(`VACUUM;`);

console.log("Merge complete! You can now use blended_markov.db");