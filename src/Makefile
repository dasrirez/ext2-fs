all: ext2_ls ext2_rm ext2_ln ext2_mkdir ext2_cp

ext2_ls:
	gcc -Wall -g ext2_ls.c helpers.c -o ext2_ls
ext2_rm:
	gcc -Wall -g ext2_rm.c helpers.c -o ext2_rm
ext2_ln:
	gcc -Wall -g ext2_ln.c helpers.c -o ext2_ln
ext2_mkdir:
	gcc -Wall -g ext2_mkdir.c helpers.c -o ext2_mkdir
ext2_cp:
	gcc -Wall -g ext2_cp.c helpers.c -o ext2_cp

clean:
	rm -rf *.0
	rm -rf ext2_ls ext2_rm ext2_ln ext2_mkdir ext2_cp
