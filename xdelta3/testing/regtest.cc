/* -*- Mode: C++ -*-  */
#include "test.h"

namespace {

// Declare constants (needed for reference-values, etc).
const xoff_t Constants::BLOCK_SIZE;

// 9 is a common value for cksum_size, but the exact value shouldn't
// matter.
const usize_t CKSUM_SIZE = 9;

// These values are filled-in by FindCksumCollision
uint8_t golden_cksum_bytes[CKSUM_SIZE] = {
  0x8d, 0x83, 0xe7, 0x6f, 0x46, 0x7f, 0xed, 0x51, 0xe6 
};
uint8_t collision_cksum_bytes[CKSUM_SIZE] = {
  0xaf, 0x55, 0x16, 0x89, 0x7c, 0x70, 0x00, 0xe5, 0xa7
};

struct TestOptions {
  TestOptions()
    : winsize(0),
      large_cksum_step(0),
      large_cksum_size(0) { }
  
  size_t winsize;
  size_t large_cksum_step;
  size_t large_cksum_size;
};

// TODO! the smatcher setup isn't working, 
void InMemoryEncodeDecode(const TestOptions &options,
			  const FileSpec &source_file, 
			  const FileSpec &target_file, 
			  Block *coded_data) {
  xd3_stream encode_stream;
  xd3_config encode_config;
  xd3_source encode_source;

  xd3_stream decode_stream;
  xd3_config decode_config;
  xd3_source decode_source;
  xoff_t verified_bytes = 0;

  if (coded_data) {
    coded_data->Reset();
  }

  memset(&encode_stream, 0, sizeof (encode_stream));
  memset(&encode_source, 0, sizeof (encode_source));

  memset(&decode_stream, 0, sizeof (decode_stream));
  memset(&decode_source, 0, sizeof (decode_source));

  xd3_init_config(&encode_config, XD3_ADLER32);
  xd3_init_config(&decode_config, XD3_ADLER32);

  encode_config.winsize = options.winsize ? options.winsize : Constants::BLOCK_SIZE;

  if (options.large_cksum_step) {
    encode_config.smatch_cfg = XD3_SMATCH_SOFT;
    encode_config.smatcher_soft.large_step = options.large_cksum_step;
  }
  if (options.large_cksum_size) {
    encode_config.smatch_cfg = XD3_SMATCH_SOFT;
    encode_config.smatcher_soft.large_look = options.large_cksum_size;
  }

  CHECK_EQ(0, xd3_config_stream (&encode_stream, &encode_config));
  CHECK_EQ(0, xd3_config_stream (&decode_stream, &decode_config));

  encode_source.size = source_file.Size();
  encode_source.blksize = Constants::BLOCK_SIZE;

  decode_source.size = source_file.Size();
  decode_source.blksize = Constants::BLOCK_SIZE;

  xd3_set_source (&encode_stream, &encode_source);
  xd3_set_source (&decode_stream, &decode_source);

  BlockIterator source_iterator(source_file);
  BlockIterator target_iterator(target_file);
  Block encode_source_block, decode_source_block;
  Block decoded_block, target_block;
  bool encoding = true;
  bool done = false;

  //DP(RINT "source %"Q"u target %"Q"u\n",
  //   source_file.Size(), target_file.Size());

  while (!done) {
    target_iterator.Get(&target_block);

    if (target_file.Blocks() == 0 ||
	target_iterator.Blkno() == (target_file.Blocks() - 1)) {
      xd3_set_flags(&encode_stream, XD3_FLUSH | encode_stream.flags);
    }

    xd3_avail_input(&encode_stream, target_block.Data(), target_block.Size());

  process:
    int ret;
    if (encoding) {
      ret = xd3_encode_input(&encode_stream);
    } else {
      ret = xd3_decode_input(&decode_stream);
    }

    //DP(RINT "%s = %s\n", encoding ? "encoding" : "decoding",
    //   xd3_strerror(ret));

    switch (ret) {
    case XD3_OUTPUT:
      if (encoding) {
	if (coded_data != NULL) {
	  // Optional encoded-output to the caller
	  coded_data->Append(encode_stream.next_out, 
			     encode_stream.avail_out);
	}
	// Feed this data to the decoder.
	xd3_avail_input(&decode_stream, 
			encode_stream.next_out, 
			encode_stream.avail_out);
	xd3_consume_output(&encode_stream);
	encoding = false;
      } else {
	decoded_block.Append(decode_stream.next_out,
			     decode_stream.avail_out);
	xd3_consume_output(&decode_stream);
      }
      goto process;

    case XD3_GETSRCBLK: {
      xd3_source *src = (encoding ? &encode_source : &decode_source);
      Block *block = (encoding ? &encode_source_block : &decode_source_block);
//       if (encoding) {
// 	DP(RINT "block %"Q"u last srcpos %"Q"u encodepos %u\n", 
// 	   encode_source.getblkno,
// 	   encode_stream.match_last_srcpos,
// 	   encode_stream.input_position);
//       }
      
      source_iterator.SetBlock(src->getblkno);
      source_iterator.Get(block);
      src->curblkno = src->getblkno;
      src->onblk = block->Size();
      src->curblk = block->Data();

      goto process;
    }

    case XD3_INPUT:
      if (!encoding) {
	encoding = true;
	goto process;
      } else {
	if (target_block.Size() < target_iterator.BlockSize()) {
	  encoding = false;
	} else {
	  target_iterator.Next();
	}
	continue;
      }

    case XD3_WINFINISH:
      if (encoding) {
	if (encode_stream.flags & XD3_FLUSH) {
	  done = true;
	  continue;
	}
	encoding = false;
      } else {
	CHECK_EQ(0, CmpDifferentBlockBytes(decoded_block, target_block));
	verified_bytes += decoded_block.Size();
	decoded_block.Reset();
	encoding = true;
      }
      goto process;

    case XD3_WINSTART:
    case XD3_GOTHEADER:
      goto process;

    default:
      CHECK_EQ(0, ret);
      CHECK_EQ(-1, ret);
    }
  }

  CHECK_EQ(target_file.Size(), verified_bytes);
  CHECK_EQ(0, xd3_close_stream(&decode_stream));
  CHECK_EQ(0, xd3_close_stream(&encode_stream));
  xd3_free_stream(&encode_stream);
  xd3_free_stream(&decode_stream);
}

//////////////////////////////////////////////////////////////////////

void TestRandomNumbers() {
  MTRandom rand;
  int rounds = 1<<20;
  uint64_t usum = 0;
  uint64_t esum = 0;

  for (int i = 0; i < rounds; i++) {
    usum += rand.Rand32();
    esum += rand.ExpRand32(1024);
  }
  
  double allowed_error = 0.01;

  uint32_t umean = usum / rounds;
  uint32_t emean = esum / rounds;

  uint32_t uexpect = UINT32_MAX / 2;
  uint32_t eexpect = 1024;

  if (umean < uexpect * (1.0 - allowed_error) ||
      umean > uexpect * (1.0 + allowed_error)) {
    cerr << "uniform mean error: " << umean << " != " << uexpect << endl;
    abort();
  }

  if (emean < eexpect * (1.0 - allowed_error) ||
      emean > eexpect * (1.0 + allowed_error)) {
    cerr << "exponential mean error: " << emean << " != " << eexpect << endl;
    abort();
  }
}

void TestRandomFile() {
  MTRandom rand1;
  FileSpec spec1(&rand1);
  BlockIterator bi(spec1);

  spec1.GenerateFixedSize(0);
  CHECK_EQ(0, spec1.Size());
  CHECK_EQ(0, spec1.Segments());
  CHECK_EQ(0, spec1.Blocks());
  bi.SetBlock(0);
  CHECK_EQ(0, bi.BytesOnBlock());

  spec1.GenerateFixedSize(1);
  CHECK_EQ(1, spec1.Size());
  CHECK_EQ(1, spec1.Segments());
  CHECK_EQ(1, spec1.Blocks());
  bi.SetBlock(0);
  CHECK_EQ(1, bi.BytesOnBlock());

  spec1.GenerateFixedSize(Constants::BLOCK_SIZE);
  CHECK_EQ(Constants::BLOCK_SIZE, spec1.Size());
  CHECK_EQ(1, spec1.Segments());
  CHECK_EQ(1, spec1.Blocks());
  bi.SetBlock(0);
  CHECK_EQ(Constants::BLOCK_SIZE, bi.BytesOnBlock());
  bi.SetBlock(1);
  CHECK_EQ(0, bi.BytesOnBlock());

  spec1.GenerateFixedSize(Constants::BLOCK_SIZE + 1);
  CHECK_EQ(Constants::BLOCK_SIZE + 1, spec1.Size());
  CHECK_EQ(2, spec1.Segments());
  CHECK_EQ(2, spec1.Blocks());
  bi.SetBlock(0);
  CHECK_EQ(Constants::BLOCK_SIZE, bi.BytesOnBlock());
  bi.SetBlock(1);
  CHECK_EQ(1, bi.BytesOnBlock());

  spec1.GenerateFixedSize(Constants::BLOCK_SIZE * 2);
  CHECK_EQ(Constants::BLOCK_SIZE * 2, spec1.Size());
  CHECK_EQ(2, spec1.Segments());
  CHECK_EQ(2, spec1.Blocks());
  bi.SetBlock(0);
  CHECK_EQ(Constants::BLOCK_SIZE, bi.BytesOnBlock());
  bi.SetBlock(1);
  CHECK_EQ(Constants::BLOCK_SIZE, bi.BytesOnBlock());
}

void TestFirstByte() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(0);
  spec1.GenerateFixedSize(1);
  CHECK_EQ(0, CmpDifferentBytes(spec0, spec0));
  CHECK_EQ(0, CmpDifferentBytes(spec1, spec1));
  CHECK_EQ(1, CmpDifferentBytes(spec0, spec1));
  CHECK_EQ(1, CmpDifferentBytes(spec1, spec0));

  spec0.GenerateFixedSize(1);
  spec0.ModifyTo(Modify1stByte(), &spec1);
  CHECK_EQ(1, CmpDifferentBytes(spec0, spec1));

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE + 1);
  spec0.ModifyTo(Modify1stByte(), &spec1);
  CHECK_EQ(1, CmpDifferentBytes(spec0, spec1));

  SizeIterator<size_t, SmallSizes> si(&rand, 0);

  for (; !si.Done(); si.Next()) {
    size_t size = si.Get();
    if (size == 0) {
      continue;
    }
    spec0.GenerateFixedSize(size);
    spec0.ModifyTo(Modify1stByte(), &spec1);
    InMemoryEncodeDecode(options, spec0, spec1, NULL);
  }
}

void TestModifyMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 3);

  struct {
    size_t size;
    size_t addr;
  } test_cases[] = {
    { Constants::BLOCK_SIZE, 0 },
    { Constants::BLOCK_SIZE / 2, 1 },
    { Constants::BLOCK_SIZE, 1 },
    { Constants::BLOCK_SIZE * 2, 1 },
  };

  for (size_t i = 0; i < SIZEOF_ARRAY(test_cases); i++) {
    ChangeList cl1;
    cl1.push_back(Change(Change::MODIFY, test_cases[i].size, test_cases[i].addr));
    spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
    CHECK_EQ(spec0.Size(), spec1.Size());
    
    size_t diff = CmpDifferentBytes(spec0, spec1);
    CHECK_LE(diff, test_cases[i].size);

    // There is a 1/256 probability of the changed byte matching the
    // original value.  The following allows double the probability to
    // pass.
    CHECK_GE(diff, test_cases[i].size - (2 * test_cases[i].size / 256));

    InMemoryEncodeDecode(options, spec0, spec1, NULL);
  }
}

void TestAddMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 2);
  // TODO: fix this test (for all block sizes)!  it's broken because
  // the same byte could be added?

  struct {
    size_t size;
    size_t addr;
    size_t expected_adds;
  } test_cases[] = {
    { 1, 0,                         2 /* 1st byte, last byte (short block) */ },
    { 1, 1,                         3 /* 1st 2 bytes, last byte */ },
    { 1, Constants::BLOCK_SIZE - 1, 2 /* changed, last */ },
    { 1, Constants::BLOCK_SIZE,     2 /* changed, last */ },
    { 1, Constants::BLOCK_SIZE + 1, 3 /* changed + 1st of 2nd block, last */ },
    { 1, 2 * Constants::BLOCK_SIZE, 1 /* last byte */ },
  };

  for (size_t i = 0; i < SIZEOF_ARRAY(test_cases); i++) {
    ChangeList cl1;
    cl1.push_back(Change(Change::ADD, test_cases[i].size, test_cases[i].addr));
    spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
    CHECK_EQ(spec0.Size() + test_cases[i].size, spec1.Size());

    Block coded;
    InMemoryEncodeDecode(options, spec0, spec1, &coded);

    Delta delta(coded);
    CHECK_EQ(test_cases[i].expected_adds,
	     delta.AddedBytes());
  }
}

void TestDeleteMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 4);

  struct {
    size_t size;
    size_t addr;
  } test_cases[] = {
    // Note: an entry { Constants::BLOCK_SIZE, 0 },
    // does not work because the xd3_srcwin_move_point logic won't
    // find a copy if it occurs >= double its size into the file.
    { Constants::BLOCK_SIZE / 2, 0 },
    { Constants::BLOCK_SIZE / 2, Constants::BLOCK_SIZE / 2 },
    { Constants::BLOCK_SIZE, Constants::BLOCK_SIZE / 2 },
    { Constants::BLOCK_SIZE * 2, Constants::BLOCK_SIZE * 3 / 2 },
    { Constants::BLOCK_SIZE, Constants::BLOCK_SIZE * 2 },
  };

  for (size_t i = 0; i < SIZEOF_ARRAY(test_cases); i++) {
    ChangeList cl1;
    cl1.push_back(Change(Change::DELETE, test_cases[i].size, test_cases[i].addr));
    spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
    CHECK_EQ(spec0.Size() - test_cases[i].size, spec1.Size());

    Block coded;
    InMemoryEncodeDecode(options, spec0, spec1, &coded);

    Delta delta(coded);
    CHECK_EQ(0, delta.AddedBytes());
  }
}

void TestCopyMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 3);

  struct {
    size_t size;
    size_t from;
    size_t to;
  } test_cases[] = {
    // Copy is difficult to write tests for because where Xdelta finds
    // copies, it does not enter checksums.  So these tests copy data from
    // later to earlier so that checksumming will start.
    { Constants::BLOCK_SIZE / 2, Constants::BLOCK_SIZE / 2, 0 },
    { Constants::BLOCK_SIZE, 2 * Constants::BLOCK_SIZE, Constants::BLOCK_SIZE, },
  };

  for (size_t i = 0; i < SIZEOF_ARRAY(test_cases); i++) {
    ChangeList cl1;
    cl1.push_back(Change(Change::COPY, test_cases[i].size, 
			 test_cases[i].from, test_cases[i].to));
    spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
    CHECK_EQ(spec0.Size() + test_cases[i].size, spec1.Size());

    Block coded;
    InMemoryEncodeDecode(options, spec0, spec1, &coded);

    Delta delta(coded);
    CHECK_EQ(0, delta.AddedBytes());
  }
}

void TestMoveMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 3);

  struct {
    size_t size;
    size_t from;
    size_t to;
  } test_cases[] = {
    // This is easier to test than Copy but has the same trouble as Delete.
    { Constants::BLOCK_SIZE / 2, Constants::BLOCK_SIZE / 2, 0 },
    { Constants::BLOCK_SIZE / 2, 0, Constants::BLOCK_SIZE / 2 },
    { Constants::BLOCK_SIZE, Constants::BLOCK_SIZE, 2 * Constants::BLOCK_SIZE },
    { Constants::BLOCK_SIZE, 2 * Constants::BLOCK_SIZE, Constants::BLOCK_SIZE },
    { Constants::BLOCK_SIZE * 3 / 2, Constants::BLOCK_SIZE, Constants::BLOCK_SIZE * 3 / 2 },

    // This is a no-op
    { Constants::BLOCK_SIZE, Constants::BLOCK_SIZE * 2, 3 * Constants::BLOCK_SIZE },
  };

  for (size_t i = 0; i < SIZEOF_ARRAY(test_cases); i++) {
    ChangeList cl1;
    cl1.push_back(Change(Change::MOVE, test_cases[i].size, 
			 test_cases[i].from, test_cases[i].to));
    spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
    CHECK_EQ(spec0.Size(), spec1.Size());

    Block coded;
    InMemoryEncodeDecode(options, spec0, spec1, &coded);

    Delta delta(coded);
    CHECK_EQ(0, delta.AddedBytes());
  }
}

void FindCksumCollision() {
  // TODO! This is not being used.
  if (golden_cksum_bytes[0] != 0) {
    CHECK(memcmp(golden_cksum_bytes, collision_cksum_bytes, CKSUM_SIZE) != 0);
    CHECK_EQ(xd3_lcksum(golden_cksum_bytes, CKSUM_SIZE),
	     xd3_lcksum(collision_cksum_bytes, CKSUM_SIZE));
    return;
  }

  MTRandom rand;
  MTRandom8 rand8(&rand);

  for (size_t i = 0; i < CKSUM_SIZE; i++) {
    collision_cksum_bytes[i] = golden_cksum_bytes[i] = rand8.Rand8();
  }

  uint32_t golden = xd3_lcksum(golden_cksum_bytes, CKSUM_SIZE);

  size_t i = 0;
  while (true) {
    collision_cksum_bytes[i++] = rand8.Rand8();

    if (golden == xd3_lcksum(collision_cksum_bytes, CKSUM_SIZE) &&
	memcmp(collision_cksum_bytes, golden_cksum_bytes, CKSUM_SIZE) != 0) {
      break;
    }

    if (i == CKSUM_SIZE) {
      i = 0;
    }
  }

  Block b1, b2;
  b1.Append(golden_cksum_bytes, CKSUM_SIZE);
  b2.Append(collision_cksum_bytes, CKSUM_SIZE);
  
  DP(RINT "Found a cksum collision\n");
  b1.Print();
  b2.Print();
}

void TestOverwriteMutator() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE);

  ChangeList cl1;
  cl1.push_back(Change(Change::OVERWRITE, 10, 0, 20));
  spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
  CHECK_EQ(spec0.Size(), spec1.Size());

  Block b0, b1;
  BlockIterator(spec0).Get(&b0);
  BlockIterator(spec1).Get(&b1);
  
  CHECK(memcmp(b0.Data(), b1.Data() + 20, 10) == 0);
  CHECK(memcmp(b0.Data(), b1.Data(), 20) == 0);
  CHECK(memcmp(b0.Data() + 30, b1.Data() + 30, Constants::BLOCK_SIZE - 30) == 0);

  cl1.clear();
  cl1.push_back(Change(Change::OVERWRITE, 10, 20, (xoff_t)0));
  spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
  CHECK_EQ(spec0.Size(), spec1.Size());

  BlockIterator(spec0).Get(&b0);
  BlockIterator(spec1).Get(&b1);
  
  CHECK(memcmp(b0.Data() + 20, b1.Data(), 10) == 0);
  CHECK(memcmp(b0.Data() + 10, b1.Data() + 10, Constants::BLOCK_SIZE - 10) == 0);
}

void TestNonBlockingProgress() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  FileSpec spec2(&rand);
  TestOptions options;

  // TODO! this test assumes block_size == 128
  CHECK_EQ(128, Constants::BLOCK_SIZE);

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE * 2);

  // This is a lazy target match
  Change ct(Change::OVERWRITE, 22, 
	    Constants::BLOCK_SIZE + 50,
	    Constants::BLOCK_SIZE + 20);

  // This is a source match just after the block boundary, shorter
  // than the lazy target match.
  Change cs1(Change::OVERWRITE, 16,
	     Constants::BLOCK_SIZE + 51,
	     Constants::BLOCK_SIZE - 1);

  // This overwrites the original source bytes.
  Change cs2(Change::MODIFY, 108,
	     Constants::BLOCK_SIZE + 20);

  // This changes the first blocks
  Change c1st(Change::MODIFY, Constants::BLOCK_SIZE - 2, 0);

  ChangeList csl;
  csl.push_back(cs1);
  csl.push_back(cs2);
  csl.push_back(c1st);

  spec0.ModifyTo(ChangeListMutator(csl), &spec1);

  ChangeList ctl;
  ctl.push_back(ct);
  ctl.push_back(c1st);

  spec0.ModifyTo(ChangeListMutator(ctl), &spec2);

  InMemoryEncodeDecode(options, spec1, spec2, NULL);
}

void TestEmptyInMemory() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;
  Block block;

  spec0.GenerateFixedSize(0);
  spec1.GenerateFixedSize(0);

  InMemoryEncodeDecode(options, spec0, spec1, &block);

  Delta delta(block);
  CHECK_LT(0, block.Size());
  CHECK_EQ(1, delta.Windows());
}

void TestBlockInMemory() {
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  TestOptions options;
  Block block;

  spec0.GenerateFixedSize(Constants::BLOCK_SIZE);
  spec1.GenerateFixedSize(Constants::BLOCK_SIZE);

  InMemoryEncodeDecode(options, spec0, spec1, &block);

  Delta delta(block);
  CHECK_EQ(1, delta.Windows());
}

void FourWayMergeTest(const TestOptions &options,
		      const FileSpec &spec0,
		      const FileSpec &spec1,
		      const FileSpec &spec2,
		      const FileSpec &spec3) {
  Block delta01, delta12, delta23;

  InMemoryEncodeDecode(options, spec0, spec1, &delta01);
  InMemoryEncodeDecode(options, spec1, spec2, &delta12);
  InMemoryEncodeDecode(options, spec2, spec3, &delta23);

  TmpFile f0, f1, f2, f3, d01, d12, d23;

  spec0.WriteTmpFile(&f0);
  spec1.WriteTmpFile(&f1);
  spec2.WriteTmpFile(&f2);
  spec2.WriteTmpFile(&f3);

  delta01.WriteTmpFile(&d01);
  delta12.WriteTmpFile(&d12);
  delta23.WriteTmpFile(&d23);

  // Merge 2
  ExtFile out;
  vector<const char*> mcmd;
  mcmd.push_back("xdelta3");
  mcmd.push_back("merge");
  mcmd.push_back("-m");
  mcmd.push_back(d01.Name());
  mcmd.push_back(d12.Name());
  mcmd.push_back(out.Name());
  mcmd.push_back(NULL);

  DP(RINT "Running one merge: %s\n", CommandToString(mcmd).c_str());
  CHECK_EQ(0, xd3_main_cmdline(mcmd.size() - 1, 
			       const_cast<char**>(&mcmd[0])));

  ExtFile recon;
  vector<const char*> tcmd;
  tcmd.push_back("xdelta3");
  tcmd.push_back("-d");
  tcmd.push_back("-s");
  tcmd.push_back(f0.Name());
  tcmd.push_back(out.Name());
  tcmd.push_back(recon.Name());
  tcmd.push_back(NULL);

  DP(RINT "Running one recon! %s\n", CommandToString(tcmd).c_str());
  CHECK_EQ(0, xd3_main_cmdline(tcmd.size() - 1, 
			       const_cast<char**>(&tcmd[0])));
  DP(RINT "Should equal! %s\n", f2.Name());

  CHECK(recon.EqualsSpec(spec2));

  /* TODO: we've only done 3-way merges, try 4-way. */
}

void TestMergeCommand1() {
  /* Repeat random-input testing for a number of iterations.
   * Test 2, 3, and 4-file scenarios (i.e., 1, 2, and 3-delta merges). */
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  FileSpec spec2(&rand);
  FileSpec spec3(&rand);

  TestOptions options;

  SizeIterator<size_t, SmallSizes> si0(&rand, 10);

  for (; !si0.Done(); si0.Next()) {
    size_t size0 = si0.Get();

    SizeIterator<size_t, SmallSizes> si1(&rand, 10);
    for (; !si1.Done(); si1.Next()) {
      size_t change1 = si1.Get();

      if (change1 == 0) {
	continue;
      }
      
      DP(RINT "S0 = %lu\n", size0);
      DP(RINT "C1 = %lu\n", change1);

      size_t add1_pos = size0 ? rand.Rand32() % size0 : 0;
      size_t del2_pos = size0 ? rand.Rand32() % size0 : 0;

      spec0.GenerateFixedSize(size0);

      ChangeList cl1, cl2, cl3;
      
      size_t change3 = change1;
      size_t change3_pos;

      if (change3 >= size0) {
	change3 = size0;
	change3_pos = 0;
      } else {
	change3_pos = rand.Rand32() % (size0 - change3);
      }

      cl1.push_back(Change(Change::ADD, change1, add1_pos));
      cl2.push_back(Change(Change::DELETE, change1, del2_pos));
      cl3.push_back(Change(Change::MODIFY, change3, change3_pos));

      spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
      spec1.ModifyTo(ChangeListMutator(cl2), &spec2);
      spec2.ModifyTo(ChangeListMutator(cl3), &spec3);

      FourWayMergeTest(options, spec0, spec1, spec2, spec3);
      FourWayMergeTest(options, spec3, spec2, spec1, spec0);
    }
  }
}

void TestMergeCommand2() {
  /* Same as above, different mutation pattern. */
  /* TODO: run this with large sizes too */
  /* TODO: run this with small sizes too */
  MTRandom rand;
  FileSpec spec0(&rand);
  FileSpec spec1(&rand);
  FileSpec spec2(&rand);
  FileSpec spec3(&rand);

  TestOptions options;

  SizeIterator<size_t, SmallSizes> si0(&rand, 10);
  for (; !si0.Done(); si0.Next()) {
    size_t size0 = si0.Get();

    SizeIterator<size_t, SmallSizes> si1(&rand, 10);
    for (; !si1.Done(); si1.Next()) {
      size_t size1 = si1.Get();

      SizeIterator<size_t, SmallSizes> si2(&rand, 10);
      for (; !si2.Done(); si2.Next()) {
	size_t size2 = si2.Get();

	SizeIterator<size_t, SmallSizes> si3(&rand, 10);
	for (; !si3.Done(); si3.Next()) {
	  size_t size3 = si3.Get();
	  
	  // We're only interested in three sizes, strictly decreasing. */
	  if (size3 >= size2 || size2 >= size1 || size1 >= size0) {
	    continue;
	  }

	  DP(RINT "S0 = %lu\n", size0);
	  DP(RINT "S1 = %lu\n", size1);
	  DP(RINT "S2 = %lu\n", size2);
	  DP(RINT "S3 = %lu\n", size3);

	  spec0.GenerateFixedSize(size0);

	  ChangeList cl1, cl2, cl3;
      
	  cl1.push_back(Change(Change::DELETE, size0 - size1, 0));
	  cl2.push_back(Change(Change::DELETE, size0 - size2, 0));
	  cl3.push_back(Change(Change::DELETE, size0 - size3, 0));

	  spec0.ModifyTo(ChangeListMutator(cl1), &spec1);
	  spec0.ModifyTo(ChangeListMutator(cl2), &spec2);
	  spec0.ModifyTo(ChangeListMutator(cl3), &spec3);

	  FourWayMergeTest(options, spec0, spec1, spec2, spec3);
	  FourWayMergeTest(options, spec3, spec2, spec1, spec0);
	}
      }
    }
  }
}

}  // anonymous namespace

int main(int argc, char **argv) {
#define TEST(x) cerr << #x << "..." << endl; x()
  TEST(TestRandomNumbers);
  TEST(TestRandomFile);
  TEST(TestFirstByte);
  TEST(TestEmptyInMemory);
  TEST(TestBlockInMemory);
  TEST(TestModifyMutator);
  TEST(TestAddMutator);
  TEST(TestDeleteMutator);
  TEST(TestCopyMutator);
  TEST(TestMoveMutator);
  TEST(TestOverwriteMutator);
  TEST(TestNonBlockingProgress);
  TEST(TestMergeCommand1);
  TEST(TestMergeCommand2);
  return 0;
}