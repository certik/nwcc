struct tar_sparse_optab
{
  void *p;
};

static struct tar_sparse_optab const star_optab;

static const void * 
f()
{
 const void *p=  &star_optab;
 return p;
}

static struct tar_sparse_optab const star_optab = {
	"hello"
};

int
main() {
	printf("%s\n", ((struct tar_sparse_optab *)f())->p); 
}
