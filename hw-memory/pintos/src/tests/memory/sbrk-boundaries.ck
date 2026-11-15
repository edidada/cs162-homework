# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sbrk-boundaries) begin
(sbrk-boundaries) end
sbrk-boundaries: exit(0)
EOF
pass;
