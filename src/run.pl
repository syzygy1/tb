#!/usr/bin/perl

$ENV{'RTBWDIR'} = '.';

use Getopt::Long;

$threads = 128;
$generate = '';
$verify = '';
$disk = '';
$min = 7;
$max = 7;
GetOptions('threads=i' => \$threads,
	   'generate' => \$generate,
	   'verify' => \$verify,
	   'min=i' => \$min,
	   'max=i' => \$max,
	   'disk' => \$disk);

sub Process {
  my($tb) = @_;
  my $len = length($tb) - 1;
  if ($len < $min || $len > $max) { return; }
  $dopt = "";
  if ($disk && $len == 7) {
    $dopt = "-d ";
  }
  if ($generate && !-e $tb.".rtbz") {
    print "Generating $tb\n";
    if ($tb !~ /.*P.*/) {
      system "./rtbgen $dopt-t $threads --stats $tb";
    } else {
      system "./rtbgenp $dopt-t $threads --stats $tb";
    }
  }
  if ($verify && -e $tb.".rtbz") {
    printf "Verifying $tb\n";
    if ($tb !~ /.*P.*/) {
      system "./rtbver -t $threads --log $tb";
    } else {
      system "./rtbverp -t $threads --log $tb";
    }
  }
}

@Pieces = ('Q', 'R', 'B', 'N', 'P');

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  $tb = "K".$a."v"."K";
  Process($tb);
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    $tb = "K".$a.$b."v"."K";
    Process($tb);
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    $tb = "K".$a."v"."K".$b;
    Process($tb);
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      $tb = "K".$a.$b.$c."v"."K";
      Process($tb);
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = 0; $k < 5; ++$k) {
      $c = @Pieces[$k];
      $tb = "K".$a.$b."v"."K".$c;
      Process($tb);
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = $k; $l < 5; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b.$c.$d."v"."K";
	Process($tb);
      }
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = 0; $l < 5; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b.$c."v"."K".$d;
	Process($tb);
      }
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $i; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = ($i == $k) ? $j : $k; $l < 5; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b."v"."K".$c.$d;
	Process($tb);
      }
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = $k; $l < 5; ++$l) {
        $d = @Pieces[$l];
        for ($m = $l; $m < 5; ++$m) {
          $e = @Pieces[$m];
          $tb = "K".$a.$b.$c.$d.$e."v"."K";
          Process($tb);
        }
      }
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = $k; $l < 5; ++$l) {
        $d = @Pieces[$l];
        for ($m = 0; $m < 5; ++$m) {
          $e = @Pieces[$m];
          $tb = "K".$a.$b.$c.$d."v"."K".$e;
          Process($tb);
        }
      }
    }
  }
}

for ($i = 0; $i < 5; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < 5; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < 5; ++$k) {
      $c = @Pieces[$k];
      for ($l = 0; $l < 5; ++$l) {
        $d = @Pieces[$l];
        for ($m = $l; $m < 5; ++$m) {
          $e = @Pieces[$m];
          $tb = "K".$a.$b.$c."v"."K".$d.$e;
          Process($tb);
        }
      }
    }
  }
}
