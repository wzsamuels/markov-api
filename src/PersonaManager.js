import 'dotenv/config';
import Database from 'better-sqlite3';
import path from 'path';

const requiredEnvVars = ['DATA_DIR', 'ENV'];
for (const envVar of requiredEnvVars) {
  if (!process.env[envVar]) {
    const error = new Error(`Missing required environment variable: ${envVar}`);
    console.error('Error loading environment variables:', error.message);
    throw error;
  }
}

class PersonaManager {
  #connections = {};

  // Retrieve an existing connection, or open a new one if it doesn't exist
  getDatabase(personaName) {
    if (!this.#connections[personaName]) {
      try {
        const dbRoot = process.env.ENV === 'production' ? '/' : process.cwd();
        const dbPath = path.join(dbRoot, `${process.env.DATA_DIR}/${personaName}_markov.db`);

        console.log(`Loading personality: ${personaName} from ${dbPath}`);

        const db = new Database(dbPath, { fileMustExist: true }); // Prevent creating an empty DB on typo
        
        // Apply optimized read settings
        db.pragma('journal_mode = WAL');
        db.pragma('synchronous = NORMAL');
        db.pragma('temp_store = MEMORY');

        this.#connections[personaName] = db;
        console.log(`Loaded personality: ${personaName}`);
      } catch (error) {
        console.error(`Failed to load personality '${personaName}':`, error.message);
        return null; 
      }
    }
    
    return this.#connections[personaName];
  }

  // Optional: Only used if you need to hot-swap a file or free memory
  unloadPersonality(personaName) {
    if (this.#connections[personaName]) {
      this.#connections[personaName].close();
      delete this.#connections[personaName];
      console.log(`Unloaded personality: ${personaName}`);
    }
  }
}

const personas = new PersonaManager();
export default personas;