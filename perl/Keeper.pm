package Ext::Keeper;

# ----------------------------------------------------------------------------
# Класс базы данных
# ----------------------------------------------------------------------------

use strict;
use vars qw($AUTOLOAD $__KEEPER);

use DBI;

use Ext::Logger;
use Ext::State;

use Ext::Model::Obj;
use Ext::Model::Tree;
use Ext::Model::Document;
use Ext::Model::Grp;
use Ext::Model::Section;

use Ext::Model::Usr;
use Ext::Model::File;
use Ext::Model::Link;

use constant Equal     => 1;
use constant Not       => 2;
use constant Greater   => 4;
use constant Less      => 8;
use constant Interval  => 16;
use constant Segment   => 32;
use constant Inset     => 64;
use constant Like      => 128;
use constant iLike     => 256;
use constant Field     => 512;
use constant Match     => 1024;
use constant Between   => 2048;
use constant AND       => 4096;
use constant OR        => 8192;

$__KEEPER = undef;

sub instance {
	if ( !$__KEEPER ) {
		$__KEEPER = new Ext::Keeper();
	}
	return $__KEEPER;
}

sub DESTROY {
	shift->disconnect();
}

sub new {
	my @_c = caller(1);
	if ( $_c[3] ne 'Ext::Keeper::instance' ) {
		my $m = 'direct call of Ext::Keeper->new() detected. use an singleton interface Ext::Keeper->instance() insteed';
		LOG->emerg($m);
		die($m);
	}

	my ($proto) = @_;

	my $class = ref($proto) || $proto;
	my $self = {};
	bless($self, $class);

	my $state = Ext::State->instance();

	# Заполним собственные свойства конкретными данными...
	$self->{db_host}     = $state->db_host();
	$self->{db_port}     = $state->db_port();

	$self->{db_name}     = $state->db_name();

	$self->{db_user}     = $state->db_user();
	$self->{db_user_password} = $state->db_user_password();

	$self->{debug} = $state->debug();
	$self->_init_();

	return $self;
}

sub authorize {
	my $self = shift;
	my $usr = shift;
	$self->{SQL}->do("SET SESSION_AUTHORIZATION TO '$usr'");
	LOG->debug("SET SESSION_AUTHORIZATION TO '$usr'");
}

sub connect {
	my $self = shift;

	$self->{SQL} = DBI->connect( 
			sprintf("dbi:Pg:dbname=%s;host=%s;port=%d", $self->{db_name}, $self->{db_host}, $self->{db_port}), 
			$self->{db_user}, 
			$self->{db_user_password}, 
			{ 'AutoCommit' => 1 } 
	) || warn "Ext Error: Не могу соединиться с базой данных\n";

	if ( $self->{SQL} ) {
		LOG->debug("Ext Debug: Создано соединение с базой данных PostgreSQL на порту ".$self->{db_port})		if  $self->{debug} ;
	}
}

sub disconnect {
	my $self= shift;
	$self->{SQL}->disconnect();
}

sub _init_ {
	my $self = shift;
	foreach my $attribute ( keys %{$self} ) {
		$self->{attributes}->{ $attribute } = 'SCALAR';
	}
}

sub last_id {
	return shift->{SQL}->last_insert_id(undef, undef, undef, undef, {sequence=>'ext_id_seq'});
}

# ----------------------------------------------------------------------------
# Обработчик ошибки. Очень важная функция, именно в ней мы будем
#  хранить все возможные коды ошибок и так далее...
# ----------------------------------------------------------------------------
sub error {
	my $self = shift;
	do { warn "Ext Error: Метод error() можно вызывать только у объектов, но не классов\n";  return undef  } unless ref($self);

	$self->{last_error} = shift || $self->{SQL}->errstr();
	chomp($self->{last_error});
	print $self->{last_error};

	warn "Ext Error: ".$self->{last_error}."\n";
}

sub _get_object_list_ {
	my $self = shift;
	my $proto = shift;

	do { warn "Ext Error: Метод get_$proto() можно вызывать только у объектов, но не классов\n";  return undef  } unless ref($self);
	
	my $namespace = shift;
	my $param = shift;
	
	my @o_param = ();
	my @list = ();

	my $light = 0;
	
	my $class = $namespace.'::'.ucfirst($proto);
	eval ("use $class");
	if ( $@ ) {
		$self->error($@);
		return 0;
	}

	my $obj = $class->new();
	my $table = $obj->table();
	my $schema = $obj->schema();

	my $sth = $self->_parse_query_( $table, $schema, $param, \$light );
	$sth->execute() || LOG->error( $DBI::errstr );
	
	while( my $row =  $sth->fetchrow_hashref() ) {
		$obj = $class->new();
		$obj->light(1) if $light;
		$obj->restore($row);
		push @list, $obj;
	}
	$sth->finish();
	return @list;
}

# my $count = Ext::Keeper->instance()->count_documents( 
#		('section', Ext::Keeper::Equal, 99), 
#		('status', ( Ext::Keeper::Equal | Ext::Keeper::Not ), 0 ) 
# );
#
sub _get_count_ {
	my $self = shift;
	my $class = shift;
	my $param = shift;

	my $count = 0;
	my $obj = $class->new();
	my $table = $class->table();
	my $fields = [ 'count(*)' ];

	my $sth = $self->_parse_query_( $table, $fields, $param, undef );
	$sth->execute();
	
	($count) = $sth->fetchrow_array();
	$sth->finish();
	
	return $count;
}

sub _get_tree_ {
	my $self = shift;
	my $class = shift;
	my @param = @_;

	my $head = {};

	return $head;
}

sub AUTOLOAD {
	my $self = shift;
	if ($AUTOLOAD =~ /\:\:count_(.+)$/) {
		return $self->_get_count_($1, @_);
	} elsif ($AUTOLOAD =~ /\:\:get_(.+)_tree$/) {
		return $self->_get_tree_($1, @_);
	} elsif ($AUTOLOAD =~ /\:\:get_(.+)$/) {
		return $self->_get_object_list_($1, @_);
	}
}

sub _parse_query_ {
	my $self = shift;
	my $table = shift;
	my $fields = shift;
	my $param = shift;
	my $light = shift;

	my $sql = undef;
	
	my ( @wheres, @binds );
	my ( $limit, $offset );
	my ( $orders, $groups );

	for( my $i = 0; $i < scalar(@$param); $i++ ) {
		my $value = $param->[$i];
		my $name = shift @$value;
		if ( $name eq 'order' ) {
			$orders = ' ORDER BY ' . ( join ', ', @$value );
		} elsif ( $name eq 'group' ) {
			$groups = ' GROUP BY ' . ( join ', ', @$value );
		} elsif ( $name eq 'limit' ) {
			$limit = sprintf ' LIMIT %d', $value->[0];
		} elsif ( $name eq 'offset' ) {
			$offset = sprintf ' OFFSET %d', $value->[0];
		} elsif ( $name eq 'light' ) {
			$$light = 1;
		} else {
			$self->_parse_param_( $name, $value, \@wheres, \@binds );
		}
	}

	$sql = sprintf 'SELECT %s FROM %s', ( join ', ', @$fields ), $table;
	if ( length(@wheres) ) {
		$sql .= sprintf ' WHERE %s', join ' ', @wheres;
	}

	$sql .= $groups;
	$sql .= $orders;
	$sql .= $limit;
	$sql .= $offset;	

	LOG->info( $sql );
	my $sth = $self->{SQL}->prepare( $sql );

	for( my $i = 0; $i < scalar( @binds ); $i++ ) {
		LOG->debug( "BIND[$i]: ". $binds[$i] );
		$sth->bind_param( $i + 1, $binds[$i] );
	}
	return $sth;
}

sub _parse_param_ {
	my $self = shift;
	my $name = shift;
	my $value = shift;
	my $wheres = shift;
	my $binds = shift;

	my $flag = shift @$value;

	if ( $flag & Ext::Keeper::AND ) {
		push @$wheres, 'AND';
		$flag ^= Ext::Keeper::AND;
	} elsif ( $flag & Ext::Keeper::OR ) {
		push @$wheres, 'OR';
		$flag ^= Ext::Keeper::OR;
	}

	if ( $flag & Ext::Keeper::Equal ) {
		push @$wheres, sprintf '( %s = ? )', $name;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Equal | Ext::Keeper::Field) ) {
		push @$wheres, sprintf '( %s = %s )', $name, $value->[0];
	} elsif ( $flag & (Ext::Keeper::Equal | Ext::Keeper::Not) ) {
		push @$wheres, sprintf '( %s <> ? )', $name;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Equal | Ext::Keeper::Not | Ext::Keeper::Field) ) {
		push @$wheres, sprintf '( %s <> %s )', $name, $value->[0];
	} elsif ( $flag & Ext::Keeper::Greater ) {
		push @$wheres, sprintf '( %s > ? )', $name;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Greater | Ext::Keeper::Field ) ) {
		push @$wheres, sprintf '( %s > %s )', $name, $value->[0];
	} elsif ( $flag & Ext::Keeper::Less ) {
		push @$wheres, sprintf '( %s < ? )', $name;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Less | Ext::Keeper::Field ) ) {
		push @$wheres, sprintf '( %s < %s )', $name, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Equal | Ext::Keeper::Greater ) ) {
		push @$wheres, sprintf '( %s >= ? )', $name;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Less | Ext::Keeper::Greater ) ) {
		push @$wheres, sprintf '( %s <= %s )', $name, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Equal | Ext::Keeper::Greater | Ext::Keeper::Field ) ) {
		push @$wheres, sprintf '( %s >= %s )', $name, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Equal | Ext::Keeper::Less | Ext::Keeper::Field ) ) {
		push @$wheres, sprintf '( %s <= %s )', $name, $value->[0];
	} elsif ( $flag & Ext::Keeper::Interval ) {
		push @$wheres, sprintf '( %s > ? ) And ( %s < ? )', $name, $name;
		push @$binds, $value->[0];
		push @$binds, $value->[1];
	} elsif ( $flag & Ext::Keeper::Segment ) {
		push @$wheres, sprintf '( %s >= ? ) And ( %s <= ? )', $name, $name;
		push @$binds, $value->[0];
		push @$binds, $value->[1];
	} elsif ( $flag & Ext::Keeper::Inset ) {
		push @$wheres, sprintf '( %s IN (?) )', $name ;
		push @$binds, $value->[0];
	} elsif ( $flag & ( Ext::Keeper::Inset | Ext::Keeper::Not ) ) {
		push @$wheres, sprintf '( %s Not IN (?) )', $name ;
		push @$binds, $value->[0];
	} elsif ( $flag & Ext::Keeper::Like ) {
		push @$wheres, sprintf '( %s LIKE ? )', $name ;
		push @$binds, $value->[0];
	} elsif ( $flag & Ext::Keeper::iLike ) {
		push @$wheres, sprintf '( %s ILIKE ? )', $name ;
		push @$binds, $value->[0];
	}
}


1;
