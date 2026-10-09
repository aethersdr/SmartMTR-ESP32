CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
host_test: host_test.c ../main/flex_api.c ../main/model.c ../main/menu_items.c
	$(CC) $(CFLAGS) -DKNOB_HOST_TEST -o $@ $^ -lm
check: host_test
	./host_test
clean:
	rm -f host_test
.PHONY: check clean
