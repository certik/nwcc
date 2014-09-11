/*
 * 05/26/09: We run out of registers here when using
 * PIC. In particular, at the end it needs a 16bit and
 * 8bit register, both of which requests can't be
 * fulfilled
 *
 * Triggered by tcl
 */
int main() {
struct subre {
 char flags;
};
 struct subre *branch;
 struct subre nonsense;
 branch = &nonsense;

  if ((branch->flags &~ branch->flags) != 0)
   branch->flags |= branch->flags;
}
