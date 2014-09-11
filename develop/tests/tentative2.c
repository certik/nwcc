struct tar_sparse_optab
{
  void *x;
};

static struct tar_sparse_optab const pax_optab;

static int
pax_decode_header ()
{
  return 1;
}

static struct tar_sparse_optab const pax_optab = {
  pax_decode_header
};

int
main() {
}

