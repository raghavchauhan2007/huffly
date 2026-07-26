/*
HUFF2412 File Format Specification (Encoder v1 / Decoder v1)

Overview
--------
This program writes and reads a Huffman-compressed file format. The output file
is:

  [HEADER (byte-aligned fields + bit-packed tree)] [PAYLOAD (bit-packed codes)]

"Payload" = the compressed bitstream for the original file contents, produced by
replacing each input byte with its Huffman code bits.

IMPORTANT: Multi-byte integers are written using the platform's native in-memory
representation (typically little-endian). This format is therefore NOT portable
across architectures with different endianness unless an explicit byte order is
used.

Header Layout (in order)
------------------------
1) Magic (8 bytes)
   ASCII bytes: 'H' 'U' 'F' 'F' '2' '4' '1' '2'
   Used to identify files produced by this compressor.

2) Original Size (uint64_t, 8 bytes)
   Exact number of bytes in the original uncompressed input file.
   Decoder MUST stop after producing exactly this many bytes (ignores any final
   padding).

3) Stored Filename Length (uint16_t, 2 bytes)
   Length in bytes of the stored filename string.

4) Stored Filename (nameLen bytes)
   Raw bytes of the input file base name (includes extension), not
NUL-terminated. Example: "photo.png" or "archive.tar.gz".

   Decoder behavior:
   - The decoder attempts to write output to this filename in the current
directory.
   - If the file already exists, the decoder prompts the user before
overwriting.

5) Tree Bit Length (uint32_t, 4 bytes)
   Number of bits used to encode the serialized Huffman tree that immediately
follows. This allows the decoder to know exactly where the tree ends and where
the payload begins.

6) Serialized Huffman Tree (treeBitsLen bits, bit-packed)
   Preorder serialization (MSB-first bit order within each byte):
     - Leaf node:
         write bit 1
         then write 8 bits of the leaf byte value
       Total = 9 bits per leaf.

     - Internal node:
         write bit 0
         then serialize left subtree
         then serialize right subtree
       Total = 1 bit + left subtree bits + right subtree bits.

7) Tree Padding (0..7 bits)
   After the tree bits are written, the encoder pads with 0 bits up to the next
   byte boundary so that the payload starts on a byte boundary.
   Decoder should discard these padding bits by aligning to the next byte
boundary.

Payload Layout
--------------
8) Huffman Payload Bits (variable length, bit-packed)
   For each input byte, the encoder writes the corresponding Huffman code bits
   (from the code table built from the Huffman tree).
   Bit order is MSB-first within output bytes, matching the BitWriter/BitReader.

9) Final Padding (0..7 bits)
   After encoding all symbols, the encoder pads the last output byte with 0 bits
   (if needed) and flushes it.
   The decoder ignores any remaining padding bits because it stops after
outputting exactly Original Size bytes.

Notes / Constraints / Error Handling
------------------------------------
- Empty input files are currently rejected by the encoder.
- Codes are limited to 64 bits in this implementation (ERR_RECURSION_DEPTH if
exceeded).
- Decoder detects invalid/corrupt streams as ERR_FORMAT (bad magic, invalid tree
encoding, impossible traversal, mismatched treeBitsLen consumption, etc.).
- I/O failures while reading return ERR_READ; I/O failures while writing return
ERR_WRITE.
- If the user declines overwrite when output already exists, the decoder returns
ERR_ABORTED.
*/

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define BUFFER_SIZE 8192
#define FREQ_SIZE 256

/**
 * Enum for error handling
 * */

typedef enum {
  SUCCESS,
  ERR_MEMORY,
  ERR_FILE,
  ERR_READ,
  ERR_HEAP_CAPACITY_FULL,
  ERR_RECURSION_DEPTH,
  ERR_WRITE,
  ERR_FORMAT,
  ERR_ABORTED
} Status;

/**
 * Reads the input file and generates frequency table for the characters
 *
 * @param filename file to be read
 * @param freq pointer to freq table
 * @param totalBytes pointer to variable totalbytes which stores the total bytes
 * read here
 *
 * @return SUCCESS if file was read, table was generated and function returned
 * cleanly
 * */

/*========================= READ FILE ===========================*/

Status readFile(const char *filename, uint64_t **freq, uint64_t *totalBytes) {
  FILE *f = fopen(filename, "rb");

  if (!f) {
    return ERR_FILE;
  }

  *freq = (uint64_t *)calloc(FREQ_SIZE, sizeof(uint64_t));
  if (!*freq) {
    fclose(f);
    return ERR_MEMORY;
  }

  uint8_t *buffer = (uint8_t *)malloc(BUFFER_SIZE);
  if (!buffer) {
    free(*freq);
    *freq = NULL;
    fclose(f);
    return ERR_MEMORY;
  }

  size_t bytesRead = 0;
  *totalBytes = 0;

  while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
    *totalBytes += bytesRead;

    for (size_t i = 0; i < bytesRead; i++) {
      (*freq)[buffer[i]]++;
    }
  }

  if (ferror(f)) {
    free(*freq);
    *freq = NULL;
    free(buffer);
    fclose(f);
    return ERR_READ;
  }

  free(buffer);
  fclose(f);
  return SUCCESS;
}

/*============================== NODE ===============================*/

typedef struct Node {
  uint8_t byte;
  uint64_t freq;
  struct Node *left;
  struct Node *right;
} Node;

/**
 * Creates a newnode and updates it's byte, freq, left and right fields
 * with appropriate values
 * */

Status createNode(Node **newNode, uint8_t byte, uint64_t freq, Node *left,
                  Node *right) {
  *newNode = (Node *)malloc(sizeof(Node));
  if (!*newNode) {
    return ERR_MEMORY;
  }

  (*newNode)->byte = byte;
  (*newNode)->freq = freq;
  (*newNode)->left = left;
  (*newNode)->right = right;

  return SUCCESS;
}

/**
 * Frees the tree structure recursively
 * */

void freeTree(Node **root) {
  if (!root || !*root)
    return;
  freeTree(&(*root)->left);
  freeTree(&(*root)->right);
  free(*root);
  *root = NULL;
}

typedef struct {
  size_t leaves;
  size_t internalNodes;
  size_t totalNodes;
  size_t height;
} TreeStats;

size_t collectTreeStats(Node *root, TreeStats *stats) {
  if (!root) {
    return 0;
  }

  stats->totalNodes++;

  if (!root->right && !root->left) {
    stats->leaves++;
  }

  else {
    stats->internalNodes++;
  }

  size_t left = collectTreeStats(root->left, stats);
  size_t right = collectTreeStats(root->right, stats);

  size_t height = 1 + (left > right ? left : right);

  if (height > stats->height) {
    stats->height = height;
  }

  return height;
}

void printTreeJSON(const Node *root, size_t *nextId, char *code, size_t depth) {
  if (!root) {
    printf("null");
    return;
  }

  size_t id = (*nextId)++;

  bool isLeaf = root->left == NULL && root->right == NULL;

  printf("{");

  printf("\"id\":%zu,", id);

  if (isLeaf) {
    code[depth] = '\0';

    printf("\"leaf\":true,");
    printf("\"value\":%u,", root->byte);
    printf("\"code\":\"%s\"", code);
  } else {
    printf("\"leaf\":false,");

    printf("\"left\":");
    code[depth] = '0';
    printTreeJSON(root->left, nextId, code, depth + 1);

    printf(",");

    printf("\"right\":");
    code[depth] = '1';
    printTreeJSON(root->right, nextId, code, depth + 1);
  }

  printf("}");
}
/*============================== HEAP ===============================*/

typedef struct {
  Node **arr;
  size_t size;
  size_t capacity;
} MinHeap;

void swap(Node **a, Node **b) {
  Node *tmp = *a;
  *a = *b;
  *b = tmp;
}

/**
 * Implements the heapify down algorithm recursively
 * */

void heapifyDown(MinHeap *h, size_t i) {
  size_t smallest = i;
  size_t left = 2 * i + 1;
  size_t right = 2 * i + 2;

  if (left < h->size && h->arr[left]->freq < h->arr[smallest]->freq) {
    smallest = left;
  }

  if (right < h->size && h->arr[right]->freq < h->arr[smallest]->freq) {
    smallest = right;
  }

  if (smallest != i) {
    swap(&h->arr[i], &h->arr[smallest]);
    heapifyDown(h, smallest);
  }
}

/**
 * Implements the heapify up algorithm recursively
 * */

void heapifyUp(MinHeap *h, size_t i) {
  if (i == 0) {
    return;
  }

  size_t parent = (i - 1) / 2;

  if (h->arr[i]->freq < h->arr[parent]->freq) {
    swap(&h->arr[i], &h->arr[parent]);
    heapifyUp(h, parent);
  }
}

/**
 * Insert a node in the min heap and calls heapify up
 * */

Status heapInsert(MinHeap *h, Node *node) {
  if (h->size >= h->capacity)
    return ERR_HEAP_CAPACITY_FULL;

  h->arr[h->size] = node;
  heapifyUp(h, h->size);
  h->size++;

  return SUCCESS;
}

/**
 * Extracts the root node from the heap and calls heapify down
 * */

Node *extractMin(MinHeap *h) {
  if (h->size == 0)
    return NULL;

  Node *min = h->arr[0];
  h->arr[0] = h->arr[h->size - 1];
  h->size--;

  heapifyDown(h, 0);
  return min;
}

/**
 * Frees the heap structure
 * */

void destroyHeapStruct(MinHeap **h) {
  if (!h || !*h)
    return;
  free((*h)->arr);
  free(*h);
  *h = NULL;
}

/**
 * Frees the heap structure along with all the nodes inside
 * via calling freeTree()
 *
 * It is to be used when tree is not formed and heap owns all the nodes
 * */

void destroyHeapWithNodes(MinHeap **h) {
  if (!h || !*h)
    return;

  if ((*h)->arr) {
    for (size_t i = 0; i < (*h)->size; i++) {
      freeTree(&(*h)->arr[i]);
    }
  }

  free((*h)->arr);
  free(*h);
  *h = NULL;
}

/*============================ BUILD HEAP ==============================*/

/**
 * Builds the heap structure using the freq table
 * */

Status buildHeap(MinHeap **h, const uint64_t *freq) {
  *h = NULL;
  Status status = SUCCESS;

  *h = (MinHeap *)malloc(sizeof(MinHeap));
  if (!*h) {
    return ERR_MEMORY;
  }

  (*h)->capacity = FREQ_SIZE;
  (*h)->size = 0;
  (*h)->arr = (Node **)malloc((*h)->capacity * sizeof(Node *));
  if (!(*h)->arr) {
    free(*h);
    return ERR_MEMORY;
  }

  for (size_t i = 0; i < FREQ_SIZE; i++) {
    if (freq[i] > 0) {
      Node *newNode = NULL;

      status = createNode(&newNode, (uint8_t)i, freq[i], NULL, NULL);
      if (status != SUCCESS)
        goto cleanup;

      status = heapInsert(*h, newNode);
      if (status != SUCCESS) {
        free(newNode);
        goto cleanup;
      }
    }
  }

  return SUCCESS;

cleanup:
  destroyHeapWithNodes(h);
  return status;
}

/*======================= BUILD HUFFMAN TREE ==========================*/

/**
 * Builds the huffman tree using the minheap generated by buildHeap()
 * */

Status buildHuffmanTree(MinHeap *h, Node **root) {
  Status status = SUCCESS;
  *root = NULL;

  if (!h || h->size == 0) {
    return SUCCESS;
  }

  while (h->size > 1) {
    Node *a = extractMin(h);
    Node *b = extractMin(h);

    if (!a || !b) {
      freeTree(&a);
      freeTree(&b);
      return ERR_FILE;
    }

    Node *merged = NULL;

    status = createNode(&merged, 0, a->freq + b->freq, a, b);
    if (status != SUCCESS) {
      freeTree(&a);
      freeTree(&b);
      return status;
    }

    status = heapInsert(h, merged);
    if (status != SUCCESS) {
      freeTree(&merged);
      return status;
    }
  }

  *root = extractMin(h);
  h->size = 0;
  return SUCCESS;
}

/**
 * Recursively computes the number of bits used to store the tree in the header
 * */

uint64_t treeBitLength(const Node *root) {
  if (!root)
    return 0;

  if (!root->left && !root->right) {
    return 1 + 8;
  }

  return 1 + treeBitLength(root->left) + treeBitLength(root->right);
}

/*======================= GENERATE HUFFMAN CODES =========================*/

typedef struct {
  uint64_t bits;
  uint8_t length;
} Code;

Status initTable(Code **table) {
  *table = (Code *)calloc(256, sizeof(Code));
  if (!*table)
    return ERR_MEMORY;
  return SUCCESS;
}

/**
 * Recursively builds the huffman codes for the characters in the frequency
 * table via traversing the tree, assigning 0 when taking the left branch and 1
 * when taking the right branch
 *
 * It handles the edge case in which a file may contain a single character by
 * a single bit code of length 1
 *
 * In this version characters whose bit pattern exceed 64 bits aren't supported
 * as uint64_t is unsigned 64 bit integer which cannot store more than 64 bits
 * */

Status buildCodes(const Node *root, Code *table, uint64_t code, uint8_t depth) {
  if (!root)
    return SUCCESS;

  if (depth >= 64) {
    return ERR_RECURSION_DEPTH;
  }

  // LEAF
  if (!root->left && !root->right) {
    // single symbol case
    if (depth == 0) {
      table[root->byte].bits = 0;
      table[root->byte].length = 1;
    }

    else {
      table[root->byte].bits = code;
      table[root->byte].length = depth;
    }

    return SUCCESS;
  }

  Status s;

  // go left -> 0
  s = buildCodes(root->left, table, (code << 1) | 0, depth + 1);
  if (s != SUCCESS)
    return s;

  // go right -> 1
  s = buildCodes(root->right, table, (code << 1) | 1, depth + 1);
  if (s != SUCCESS)
    return s;

  return SUCCESS;
}

/*===================== BIT WRITER AND UTILS =======================*/

typedef struct {
  FILE *f;
  uint8_t buffer;
  uint8_t bitCount;
} BitWriter;

/**
 * Initializes BitWriter structure and opens the output file
 *
 * buffer and bitcount are initialized to 0
 * */

Status initBitWriter(BitWriter *bw, const char *filename) {
  bw->f = fopen(filename, "wb");
  if (!bw->f) {
    return ERR_FILE;
  }

  bw->buffer = bw->bitCount = 0;
  return SUCCESS;
}

/**
 * Writes the bits into buffer first and packs them into a byte
 * Once the buffer is full of 8 bits it is flushed to the file
 *
 * Bits are written in MSB first order
 * */

Status writeBits(BitWriter *bw, uint64_t bits, uint8_t length) {
  for (int i = length - 1; i >= 0; i--) {
    uint8_t bit = (uint8_t)((bits >> i) & 1);

    bw->buffer = (uint8_t)((bw->buffer << 1) | bit);
    bw->bitCount++;

    if (bw->bitCount == 8) {
      if (fwrite(&bw->buffer, 1, 1, bw->f) != 1)
        return ERR_WRITE;

      bw->buffer = 0;
      bw->bitCount = 0;
    }
  }

  return SUCCESS;
}

/**
 * At the end the file may have less than 8 bits
 *
 * To handle this we write the bits following MSB first ordering
 * and then pad the remaining bits to 0 and then flush to the file
 * */

Status writePaddedBits(BitWriter *bw) {
  // pads leftover bits with 0s and writes them to file
  if (bw->bitCount == 0)
    return SUCCESS;
  bw->buffer = (uint8_t)(bw->buffer << (8 - bw->bitCount));

  if (fwrite(&bw->buffer, 1, 1, bw->f) != 1)
    return ERR_WRITE;

  bw->buffer = bw->bitCount = 0;
  return SUCCESS;
}

Status closeBitWriter(BitWriter *bw) {
  if (!bw || !bw->f)
    return SUCCESS;
  if (fclose(bw->f) != 0)
    return ERR_FILE;
  bw->f = NULL;
  return SUCCESS;
}

/**
 * Utility function to extract the filename from full path
 * */

const char *baseName(const char *path) {
  if (!path)
    return "";

  const char *last = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\')
      last = p + 1;
  }
  return last;
}

/*========================= HEADER ============================*/

/**
 * Writes the huffman tree to the compressed file using preorder traversal
 *
 * Internal nodes are written as 0 bit followed by the left subtree and the
 * right subtree
 *
 * Leaf nodes are written as 1 bit followed by 8 bit byte value
 * */

Status writeTree(const Node *root, BitWriter *bw) {
  if (!root)
    return ERR_FILE;

  Status s;

  // leaf
  if (!root->left && !root->right) {
    s = writeBits(bw, 1, 1);
    if (s != SUCCESS)
      return s;

    s = writeBits(bw, root->byte, 8);
    if (s != SUCCESS)
      return s;

    return SUCCESS;
  }

  // internal
  s = writeBits(bw, 0, 1);
  if (s != SUCCESS)
    return s;

  s = writeTree(root->left, bw);
  if (s != SUCCESS)
    return s;

  s = writeTree(root->right, bw);
  if (s != SUCCESS)
    return s;

  return SUCCESS;
}

/**
 * Writes the header in the output file that will help the decoder
 * to reconstruct the original bit pattern of the file
 *
 * The function writes the following in the header:
 *  MAGIC BYTES = 'H','U','F','F','2','4','1','2'
 *
 *  The original file size which helps the decoder to know after how many bytes
 * it has to stop
 *
 *  Size of the orginal filename
 *
 *  The original filename which is given to the uncompressed file by the decoder
 *
 *  Length of the huffman tree which is embedded into the header
 *
 *  The huffman tree which is flattened into bits and then written in header
 *  and will later be reconstructed by the decoder
 *
 *  Also any remaining bits are padded to zeroes so that there is no
 *  ambiguity left for the decoder
 * */

Status writeHeader(BitWriter *bw, uint64_t originalSize, const Node *root,
                   const char *inputPath) {
  const uint8_t magic[] = {'H', 'U', 'F', 'F', '2', '4', '1', '2'};

  if (fwrite(magic, 1, 8, bw->f) != 8)
    return ERR_WRITE;

  if (fwrite(&originalSize, sizeof(originalSize), 1, bw->f) != 1)
    return ERR_WRITE;

  const char *name = baseName(inputPath);
  size_t n = strlen(name);
  if (n > UINT16_MAX)
    return ERR_FILE;

  uint16_t nameLen = (uint16_t)n;

  if (fwrite(&nameLen, sizeof(nameLen), 1, bw->f) != 1)
    return ERR_WRITE;
  if (nameLen > 0) {
    if (fwrite(name, 1, nameLen, bw->f) != nameLen)
      return ERR_WRITE;
  }

  uint64_t treeBits64 = treeBitLength(root);
  if (treeBits64 > UINT32_MAX)
    return ERR_FILE;

  uint32_t treeBitsLen = (uint32_t)treeBits64;
  if (fwrite(&treeBitsLen, sizeof(treeBitsLen), 1, bw->f) != 1)
    return ERR_WRITE;

  Status s = writeTree(root, bw);
  if (s != SUCCESS)
    return s;

  s = writePaddedBits(bw);
  if (s != SUCCESS)
    return s;

  return SUCCESS;
}

/*========================= ENCODER ===========================*/

/**
 * First header is written in the output file
 *
 * Then the input file is re-read in chunks of 8192 bytes and
 * simultaneously output file is written bit by bit using the bitwriter
 *
 * Then at last any remaining bits are padded with 0
 * */

Status encodeFile(const char *inName, const char *outName, const Code *table,
                  uint64_t originalSize, const Node *root) {
  Status s = SUCCESS;
  FILE *in = NULL;
  uint8_t *buffer = NULL;
  BitWriter bw = {0};

  in = fopen(inName, "rb");
  if (!in)
    return ERR_FILE;

  s = initBitWriter(&bw, outName);
  if (s != SUCCESS)
    goto cleanup;

  buffer = (uint8_t *)malloc(BUFFER_SIZE);
  if (!buffer) {
    s = ERR_MEMORY;
    goto cleanup;
  }

  s = writeHeader(&bw, originalSize, root, inName);
  if (s != SUCCESS)
    goto cleanup;

  size_t bytesRead = 0;

  while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, in)) > 0) {
    for (size_t i = 0; i < bytesRead; i++) {
      uint8_t byte = buffer[i];

      if (table[byte].length == 0) {
        s = ERR_FILE;
        goto cleanup;
      }

      s = writeBits(&bw, table[byte].bits, table[byte].length);
      if (s != SUCCESS)
        goto cleanup;
    }
  }

  if (ferror(in)) {
    s = ERR_READ;
    goto cleanup;
  }

  s = writePaddedBits(&bw);
  if (s != SUCCESS)
    goto cleanup;

cleanup:
  if (in)
    fclose(in);

  Status cs = closeBitWriter(&bw);
  if (s == SUCCESS && cs != SUCCESS)
    s = cs;

  free(buffer);
  return s;
}

/*========================== HEADER READER =========================*/

typedef struct {
  uint64_t originalSize;
  uint16_t nameLen;
  char *name;
  uint32_t treeBitsLen;
} HuffHeader;

void freeHeader(HuffHeader *h) {
  if (!h)
    return;
  free(h->name);
  h->name = NULL;
}

/**
 * Now the encoded file is to be read and we want to get
 * a one-to-one copy of the original file
 *
 * First header is read in the same order like it was written
 *
 * MAGIC BYTES WHICH IDENTIFY HUFF FILE FORMAT
 * ORIGINAL SIZE OF THE FILE
 * LENGTH OF ORIGINAL FILE NAME
 * THE ORIGINAL FILE NAME
 * THE SERIALIZED HUFFMAN TREE
 * */

Status readHeader(FILE *in, HuffHeader *h) {
  memset(h, 0, sizeof(*h));

  uint8_t magic[8];
  const uint8_t magic_expected[] = {'H', 'U', 'F', 'F', '2', '4', '1', '2'};

  if (fread(magic, 1, 8, in) != 8)
    return ERR_READ;
  if (memcmp(magic, magic_expected, 8) != 0)
    return ERR_FORMAT;

  if (fread(&h->originalSize, sizeof(h->originalSize), 1, in) != 1)
    return ERR_READ;
  if (fread(&h->nameLen, sizeof(h->nameLen), 1, in) != 1)
    return ERR_READ;

  h->name = (char *)malloc((size_t)h->nameLen + 1);
  if (!h->name)
    return ERR_MEMORY;

  if (h->nameLen > 0) {
    if (fread(h->name, 1, h->nameLen, in) != h->nameLen)
      return ERR_READ;
  }
  h->name[h->nameLen] = '\0';

  if (fread(&h->treeBitsLen, sizeof(h->treeBitsLen), 1, in) != 1)
    return ERR_READ;

  return SUCCESS;
}

/*======================== BIT READER ========================*/

typedef struct {
  FILE *f;
  uint8_t buffer;
  uint8_t bitPos;
} BitReader;

void initBitReader(BitReader *br, FILE *f) {
  br->f = f;
  br->buffer = 0;
  br->bitPos = 0;
}

/**
 * Reads a byte from the file and flushes it in the buffer
 *
 * Next read the buffer bit by bit and update the bits read
 * in bitPos member of the BitReader struct
 *
 * Bits are read in MSB first order
 * */

Status readBit(BitReader *br, uint8_t *outBit) {
  if (br->bitPos == 0) {
    if (fread(&br->buffer, 1, 1, br->f) != 1)
      return ERR_READ;
    br->bitPos = 8;
  }

  *outBit = (br->buffer >> (br->bitPos - 1)) & 1;
  br->bitPos--;
  return SUCCESS;
}

/**
 * Multiple bits are read simultaneously and stored in
 * unsigned 64 bit integer
 *
 * Bits are read one at a time using readBit() and combined
 * into final value using left shift operation
 * */

Status readBitsU64(BitReader *br, uint32_t bitCount, uint64_t *outValue) {
  uint64_t value = 0;

  for (uint32_t bitIdx = 0; bitIdx < bitCount; bitIdx++) {
    uint8_t bit = 0;

    Status s = readBit(br, &bit);
    if (s != SUCCESS)
      return s;

    value = (value << 1) | (uint64_t)bit;
  }

  *outValue = value;
  return SUCCESS;
}

// discard remaining bits in current byte
void alignToByte(BitReader *br) { br->bitPos = 0; }

/*
Reads a Huffman tree in the same preorder bit format as writeTree(), but
enforces a hard limit: it will consume exactly treeBitsLen bits overall.

bitsLeft:
  input  = how many tree bits remain to be read
  output = decremented as bits are consumed; should reach 0 when tree is fully
read
*/

Status readTree(BitReader *br, uint32_t *bitsLeft, Node **outNode) {
  *outNode = NULL;

  if (!bitsLeft || !*bitsLeft)
    return ERR_FORMAT;

  uint8_t tag;
  Status s = readBit(br, &tag);
  if (s != SUCCESS)
    return s;
  (*bitsLeft)--;

  if (tag == 1) {
    // leaf: next 8 bits are byte value
    if (*bitsLeft < 8)
      return ERR_FORMAT;

    uint64_t byte = 0;
    s = readBitsU64(br, 8, &byte);
    if (s != SUCCESS)
      return s;
    (*bitsLeft) -= 8;

    s = createNode(outNode, (uint8_t)byte, 0, NULL, NULL);
    if (s != SUCCESS)
      return s;

    return SUCCESS;
  }

  // internal: read left subtree thn right subtree
  Node *left = NULL;
  Node *right = NULL;

  s = readTree(br, bitsLeft, &left);
  if (s != SUCCESS) {
    freeTree(&left);
    return s;
  }

  s = readTree(br, bitsLeft, &right);
  if (s != SUCCESS) {
    freeTree(&left);
    freeTree(&right);
    return s;
  }

  s = createNode(outNode, 0, 0, left, right);
  if (s != SUCCESS) {
    freeTree(&left);
    freeTree(&right);
    return s;
  }

  return SUCCESS;
}

/*======================== DECODER ==========================*/

/**
 * Flush count bits into the output file
 * */

Status flushBuffer(FILE *out, uint8_t *buffer, size_t *count) {
  if (*count == 0)
    return SUCCESS;

  if (fwrite(buffer, 1, *count, out) != *count)
    return ERR_WRITE;
  *count = 0;
  return SUCCESS;
}

/**
 * Handles the edge case first
 * if the file contained only one unique byte then it is written
 * with that byte until original size bytes are written
 *
 * Bits are read from the file and are used to traverse the tree
 * until a leaf node is encountered whose bytes are then first stored
 * in a buffer and then flushed to output file when buffer is full
 *
 * This operation continues till original size bytes are written
 * */

Status decodePayload(BitReader *br, FILE *out, const Node *root,
                     uint64_t originalSize) {
  if (!root)
    return ERR_FORMAT;

  Status s = SUCCESS;
  uint8_t *buffer = NULL;
  size_t outCount = 0;

  buffer = (uint8_t *)malloc(BUFFER_SIZE);
  if (!buffer)
    return ERR_MEMORY;

  // special case: single byte
  if (!root->left && !root->right) {
    for (uint64_t i = 0; i < originalSize; i++) {
      buffer[outCount++] = root->byte;

      if (outCount == BUFFER_SIZE) {
        s = flushBuffer(out, buffer, &outCount);
        if (s != SUCCESS)
          goto cleanup;
      }
    }

    s = flushBuffer(out, buffer, &outCount);
    if (s != SUCCESS)
      goto cleanup;

    s = SUCCESS;
    goto cleanup;
  }

  uint64_t written = 0;
  const Node *curr = root;

  while (written < originalSize) {
    uint8_t bit = 0;

    s = readBit(br, &bit);
    if (s != SUCCESS)
      goto cleanup;

    curr = bit ? curr->right : curr->left;
    if (!curr) {
      s = ERR_FORMAT;
      goto cleanup;
    }

    if (!curr->left && !curr->right) {
      buffer[outCount++] = curr->byte;
      written++;
      curr = root;

      if (outCount == BUFFER_SIZE) {
        s = flushBuffer(out, buffer, &outCount);
        if (s != SUCCESS)
          goto cleanup;
      }
    }
  }

  s = flushBuffer(out, buffer, &outCount);
  if (s != SUCCESS)
    goto cleanup;

cleanup:
  free(buffer);
  return s;
}

/*======================== UTILITIES ========================*/

/**
 * Cleanup function to provide a single path for the program
 * to terminate in case of an error or successful run
 * */

void cleanupAll(uint64_t *freq, MinHeap *h, Node *root, Code *table, char *outName) {
  free(freq);

  if (root) {
    freeTree(&root); // frees all nodes
    destroyHeapStruct(
        &h); // heap contains stale pointers, but only struct is freed
  }

  else {
    destroyHeapWithNodes(&h); // frees nodes when tree not formed
  }

  free(table);
  free(outName);
}

void printCodes(const Code *table) {
  for (int byte = 0; byte < 256; byte++) {
    if (table[byte].length == 0)
      continue;

    printf("%c: ", byte);

    uint64_t bits = table[byte].bits;
    uint8_t length = table[byte].length;

    for (uint8_t i = length; i > 0; i--) {
      putchar(((bits >> (i - 1)) & 1) ? '1' : '0');
    }

    putchar('\n');
  }
}

/**
 * Helper function to print the status codes in case of an error
 * or SUCCESS
 * */

void printStatus(Status s) {
  switch (s) {
  case SUCCESS:
    fprintf(stderr, "SUCCESS\n");
    break;

  case ERR_FILE:
    fprintf(stderr, "ERR_FILE\n");
    break;

  case ERR_MEMORY:
    fprintf(stderr, "ERR_MEMORY\n");
    break;

  case ERR_READ:
    fprintf(stderr, "ERR_READ\n");
    break;

  case ERR_RECURSION_DEPTH:
    fprintf(stderr, "ERR_RECURSION_DEPTH\n");
    break;

  case ERR_HEAP_CAPACITY_FULL:
    fprintf(stderr, "ERR_HEAP_CAPACITY_FULL\n");
    break;

  case ERR_WRITE:
    fprintf(stderr, "ERR_WRITE\n");
    break;

  case ERR_FORMAT:
    fprintf(stderr, "ERR_FORMAT\n");
    break;

  case ERR_ABORTED:
    fprintf(stderr, "ERR_ABORTED\n");
    break;
  }
}

/**
 * Helper function to generate the output name for the encoded file
 * */

Status makeOutputName(char **outName, const char *outputPath) {
  *outName = NULL;

  const char *base = outputPath;
  const char *ext = ".huff";

  size_t baseLen = strlen(base);
  size_t extLen = strlen(ext);

  *outName = (char *)malloc(baseLen + extLen + 1);
  if (!*outName)
    return ERR_MEMORY;

  memcpy(*outName, base, baseLen);
  memcpy(*outName + baseLen, ext, extLen);
  (*outName)[baseLen + extLen] = '\0';

  return SUCCESS;
}

/*==================== CMDLINE ARGS PARSING ======================*/

/**
 * Helper functions to process the command line arguments and
 * handle the case where the file already exists in the current directory
 * */

typedef enum { NONE, COMPRESS, DECOMPRESS, INSPECT } Mode;

Status parseArgs(int argc, char **argv, Mode *mode, const char **inputPath, const char **outputPath) {
  *mode = NONE;
  *inputPath = NULL;
  *outputPath = NULL;

  if (argc < 3) {
    return ERR_FILE;
  }

  if (strcmp(argv[1], "-c") == 0) {
    if (argc != 4)
      return ERR_FILE;

    *mode = COMPRESS;
    *inputPath = argv[2];
    *outputPath = argv[3];
    return SUCCESS;
  }

  else if (strcmp(argv[1], "-x") == 0) {
    if (argc != 4)
      return ERR_FILE;

    *mode = DECOMPRESS;
    *inputPath = argv[2];
    *outputPath = argv[3];
    return SUCCESS;
  }

  else if (strcmp(argv[1], "-i") == 0) {
    if (argc != 3)
      return ERR_FILE;

    *mode = INSPECT;
    *inputPath = argv[2];
    return SUCCESS;
  }

  return ERR_FILE;
}

char fileExists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f) {
    fclose(f);
    return 1;
  }

  return 0;
}

int promptOverwrite(const char *path) {
  fprintf(stderr, "File '%s' already exists. Overwrite? [y/N]: ", path);
  fflush(stderr);

  int c = getchar();
  while (c != '\n' && c != EOF) {
    int d = getchar();
    if (d == '\n' || d == EOF)
      break;
  }

  return (c == 'y' || c == 'Y');
}

/*=================== HIGH-LEVEL FILE OPS ===================*/

/**
 * These are high-level wrapper functions that just call the necessary functions
 * in order to achieve the desired task
 *
 * First the file is read to make the freq table and get the total bytes
 * then the heap is built
 *
 * Huffman tree is generated using heap
 *
 * Table to store the huffman codes is initialized and
 * huffman codes are produced by traversing the tree
 * built in the previous steps
 *
 * The file is then encoded by the encoder pipeline
 * */

Status compressFile(const char *inputPath, const char *outputPath) {
  Status s = SUCCESS;

  uint64_t *freq = NULL;
  MinHeap *heap = NULL;
  Node *root = NULL;
  uint64_t totalBytes = 0;
  Code *table = NULL;
  char *outName = NULL;

  s = readFile(inputPath, &freq, &totalBytes);
  if (s != SUCCESS)
    goto cleanup;

  if (totalBytes == 0) {
    s = ERR_FILE; // or ERR_FORMAT if you prefer
    goto cleanup;
  }

  s = buildHeap(&heap, freq);
  if (s != SUCCESS)
    goto cleanup;

  s = buildHuffmanTree(heap, &root);
  if (s != SUCCESS)
    goto cleanup;

  s = initTable(&table);
  if (s != SUCCESS)
    goto cleanup;

  s = buildCodes(root, table, 0, 0);
  if (s != SUCCESS)
    goto cleanup;

  s = makeOutputName(&outName, outputPath);
  if (s != SUCCESS)
    goto cleanup;

  s = encodeFile(inputPath, outName, table, totalBytes, root);
  if (s != SUCCESS)
    goto cleanup;

  printf("Original File Size: %" PRIu64 " Bytes\n", totalBytes);
  printf("Wrote Compressed File: %s\n", outName);

cleanup:
  cleanupAll(freq, heap, root, table, outName);
  return s;
}

/**
 * First the header is read
 * Then the actual bits are read and converted to the
 * original file
 * */

Status decodeFile(const char *inHuffPath, const char *outputDirectory) {
  Status s = SUCCESS;
  FILE *in = NULL;
  FILE *out = NULL;

  HuffHeader header;
  memset(&header, 0, sizeof(header));

  Node *root = NULL;

  in = fopen(inHuffPath, "rb");
  if (!in) {
    s = ERR_FILE;
    goto cleanup;
  }

  s = readHeader(in, &header);
  if (s != SUCCESS)
    goto cleanup;

  if (header.treeBitsLen == 0) {
    s = ERR_FORMAT;
    goto cleanup;
  }

  // if (fileExists(header.name)) {
  //   if (!promptOverwrite(header.name)) {
  //     s = ERR_ABORTED;
  //     goto cleanup;
  //   }
  // }

  size_t dirLen = strlen(outputDirectory);
  const char *separator = (dirLen > 0 && outputDirectory[dirLen - 1] == '/') ? "" : "/";

  int len = snprintf(NULL, 0, "%s%s%s", outputDirectory, separator, header.name);
  if (len < 0) {
    s = ERR_FILE;
    goto cleanup;
  }

  char *outputPath = malloc(len + 1);

  if (!outputPath) {
    s = ERR_MEMORY;
    goto cleanup;
  }

  if (snprintf(outputPath, len + 1, "%s%s%s", outputDirectory, separator, header.name) < 0) {
    s = ERR_FILE;
    goto cleanup;
  }

  out = fopen(outputPath, "wb");
  if (!out) {
    s = ERR_FILE;
    goto cleanup;
  }

  BitReader br;
  initBitReader(&br, in);

  uint32_t bitsLeft = header.treeBitsLen;
  s = readTree(&br, &bitsLeft, &root);
  if (s != SUCCESS)
    goto cleanup;

  if (bitsLeft != 0) {
    s = ERR_FORMAT;
    goto cleanup;
  }

  alignToByte(&br);

  s = decodePayload(&br, out, root, header.originalSize);
  if (s != SUCCESS)
    goto cleanup;

  // printf("Original File Size: %" PRIu64 " Bytes\n", header.originalSize);
  // printf("Wrote Decoded File: %s\n", header.name);

  printf("{\"outputPath\":\"%s\"}\n", outputPath);

cleanup:
  if (out)
    fclose(out);
  if (in)
    fclose(in);
  if (outputPath)
    free(outputPath);
  freeTree(&root);
  freeHeader(&header);
  return s;
}

Status inspectFile(const char *inHuffPath) {
  Status s = SUCCESS;
  FILE *in = NULL;

  HuffHeader header;
  memset(&header, 0, sizeof(header));

  Node *root = NULL;

  in = fopen(inHuffPath, "rb");
  if (!in) {
    s = ERR_FILE;
    goto cleanup;
  }

  s = readHeader(in, &header);
  if (s != SUCCESS)
    goto cleanup;

  if (header.treeBitsLen == 0) {
    s = ERR_FORMAT;
    goto cleanup;
  }

  BitReader br;
  initBitReader(&br, in);

  uint32_t bitsLeft = header.treeBitsLen;
  s = readTree(&br, &bitsLeft, &root);
  if (s != SUCCESS)
    goto cleanup;

  if (bitsLeft != 0) {
    s = ERR_FORMAT;
    goto cleanup;
  }

  alignToByte(&br);

  uint32_t treeBitsCount = header.treeBitsLen;
  uint64_t treeBytes = (treeBitsCount + 7) / 8;
  uint64_t treePaddingBits = treeBytes * 8 - treeBitsCount;

  uint64_t headerSize = 8 + 8 + 2 + header.nameLen + 4 + treeBytes;

  TreeStats stats = {0};

  size_t treeHeight = collectTreeStats(root, &stats);

  size_t nextId = 0;
  char code[256] = {0};

  printf("{\n");

  printf("\t\"magic\": \"HUFF2412\",\n");
  printf("\t\"originalFilename\": \"%s\",\n", header.name);
  printf("\t\"originalSize\": %" PRIu64 ",\n", header.originalSize);
  printf("\t\"serializedTreeBits\": %" PRIu32 ",\n", treeBitsCount);
  printf("\t\"headerSize\": %" PRIu64 ",\n", headerSize);
  printf("\t\"treeBytes\": %" PRIu64 ",\n", treeBytes);
  printf("\t\"treePaddingBits\": %" PRIu64 ",\n", treePaddingBits);
  printf("\t\"leaves\": %zu,\n", stats.leaves);
  printf("\t\"internal\": %zu,\n", stats.internalNodes);
  printf("\t\"total\": %zu,\n", stats.totalNodes);
  printf("\t\"height\": %zu,\n", stats.height);

  printf("\t\"tree\": ");
  printTreeJSON(root, &nextId, code, 0);

  printf("\n}\n");

cleanup:
  if (in)
    fclose(in);
  freeTree(&root);
  freeHeader(&header);
  return s;
}

/*============================ MAIN ============================*/

int main(int argc, char **argv) {
  Mode mode;
  const char *inputPath = NULL;
  const char *outputPath = NULL;
  Status s;

  s = parseArgs(argc, argv, &mode, &inputPath, &outputPath);
  if (s != SUCCESS || mode == NONE) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s -c <input_path> <output_path>    (compress)\n", argv[0]);
    fprintf(stderr, "  %s -x <input_path> <output_path>    (decompress)\n", argv[0]);
    fprintf(stderr, "  %s -i <input_file>                  (inspect)\n", argv[0]);
    return 1;
  }

  if (mode == COMPRESS)
    s = compressFile(inputPath, outputPath);
  else if (mode == DECOMPRESS)
    s = decodeFile(inputPath, outputPath);
  else
    s = inspectFile(inputPath);

  if (s != SUCCESS) {
    printStatus(s);
  }

  return (s == SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
