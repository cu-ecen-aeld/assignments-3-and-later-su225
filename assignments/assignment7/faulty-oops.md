# Kernel OOPS analysis
```
# echo "hello world" > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=000000004269d000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: faulty(O) scull(O)
CPU: 0 PID: 154 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008d1bd80
x29: ffffffc008d1bd80 x28: ffffff80026a3fc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040000000 x22: 000000000000000c x21: 000000556b172670
x20: 000000556b172670 x19: ffffff800266f500 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d1bdf0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 3530f6ed6b232789 ]---
```

## Analysis
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```
The above line gives out that this is a nil dereference. Somewhere the module doesn't check for NULL properly. The location information is in the call trace section below
```
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
```
However, it is not obvious what is going on. However, the following lines `invoke_syscall` and `__arm64_sys_write` give a clue that it is invoking `write` syscall for `arm64` architecture. This makes sense because of the command and we are emulating `arm64` in QEMU.

We have mapped `/dev/faulty` to the character device as defined by our `faulty` module. So, we need to check the `file_operations` table to see which function is invoked on `write` syscall. Looking at the source code, it is
```c
struct file_operations faulty_fops = {
	.read =  faulty_read,
	.write = faulty_write,
	.owner = THIS_MODULE
};
```
So the function invoked is `faulty_write`. Checking that function
```c
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```
This **clearly shows NULL dereference**. It is also shown on **top of the stack trace**.

## More interesting information
The kernel backtrace is also showing how it ended up with this conclusion. It shows the page table lookup
```
user pgtable: 4k pages, 39-bit VAs, pgdp=000000004269d000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
```
This means that the kernel got a fault from the MMU, tried to lookup the page table, found trouble and ended up with oops. Depending on the architecture, the page fault handler could be something like `handle_page_fault` or `do_page_fault` or something like that registered in the Interrupt Table.

## Kernel tainting information
Kernel OOPS also contains information on loading modules. It indicates G and O flags. Their meaning is explained in the [tained kernels page](https://docs.kernel.org/admin-guide/tainted-kernels.html) of the Linux kernel documentation

| Flag | Description |
|------|-------------|
| O | Externally built (out-of-tree) module is loaded |
| G | Propreitary module was loaded |