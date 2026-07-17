import json
import os

input_file = './data/soda_raw.jsonl'
output_dir = './data/soda_data'

# Ensure the output directory exists
os.makedirs(output_dir, exist_ok=True)

chunk_size = 10000
current_chunk = 1
line_count = 0

out_f = open(os.path.join(output_dir, f'soda_chunk_{current_chunk}.txt'), 'w', encoding='utf-8')

with open(input_file, 'r', encoding='utf-8') as in_f:
    for line in in_f:
        try:
            data = json.loads(line)
            
            # Extract the dialogue array
            if 'dialogue' in data:
                # Join the conversation turns with a space
                conversation = " ".join(data['dialogue'])
                out_f.write(conversation + "\n")
                line_count += 1
                
                # Roll over to a new file when the chunk limit is reached
                if line_count >= chunk_size:
                    out_f.close()
                    current_chunk += 1
                    line_count = 0
                    out_f = open(os.path.join(output_dir, f'soda_chunk_{current_chunk}.txt'), 'w', encoding='utf-8')
                    
        except json.JSONDecodeError:
            print("Skipping malformed JSON line.")
            continue

out_f.close()
print(f"Finished parsing. Generated {current_chunk} text files in {output_dir}/")