# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sbrk-bounds) begin
(sbrk-bounds) end
sbrk-bounds: exit(0)
EOF
pass;
