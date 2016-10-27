package Ext::Dispatcher;

use strict;
use Data::Dumper;

use Ext::Logger;
use Ext::State;

use vars qw($CONTROLS);
$CONTROLS = undef;

sub new {
	my $this = shift;

	my $self = {};
	bless $self, $this;

	$self->{path} = $CONTROLS;
	return $self;
}

sub delegate {
	my $self = shift;
	my $state = Ext::State->instance();
	
	$self->{path} ||= $state->mason()->interp->comp_root;

	my ($file, $controller, $action, $args) = $self->controller();

	unless ( -f $file ) {
		return $state->mason()->scomp('404.msn');
	}

	LOG->info( sprintf "Delegated to: %s", $state->project().'::' . $controller );

	my $package = $state->project() . '::' . $controller;
	eval("use $package;");
	if ($@) {
		my $e = $@;
		LOG->error($e);
		return $state->mason()->scomp('500.msn', error => $e);
	}

	my $c = $package->new();
	if ( $c->isa('Ext::Auth') ) {
		if ( $action eq 'logout' ) {
			return $c->logout();
		} elsif ( $c->authorize( $c->role() ) == 0 ) {
			LOG->debug( $state->request()->notes() );
			if ( $c->auth_required() ) {
				return $state->mason()->scomp('403.msn');
			}
		}
	}

	my $out;
	
	unless( $c->can($action) ) {
		$action = 'index';
	}

	my $out = $c->can($action) ? $c->$action( @$args ) : $state->mason()->scomp('404.msn');

	if ( $c->isa('Ext::Layout') ) {
		return $c->assemble( $out );
	}
	return $out;
}

sub controller {
	my $self = shift;
	
	my $route = Ext::State->instance()->request()->uri;
	$route =~ s/^\///g;
	$route =~ s/\.(.+?)$//g;

	my $path = $self->{path};

	my $file = undef;
	my $action = undef;
	my $controller = undef;
	
	my @argv = ();
	my @args = split /\s*\/+?\s*/, $route;
	my $argc = scalar(@args);

	my @_args = @args;

	for(my $i = 0; $i < $argc; $i++ ) {
		$args[$i] = ucfirst($args[$i]);
	}

	while( scalar(@args) ) {
		my $p = $path . '/'. (join '/', @args);
		if ( -f $p.'.pm' ) {
			last;
		} else {
			pop @args;
			unshift @argv, (pop @_args);
		}
	}

	if ( scalar(@args) ) {
		$file = $path . '/'. (join '/', @args) . '.pm';
		$controller = join '::', @args;
	} else {
		$file = $path . '/Controller.pm';
		$controller = 'Controller';
	}

	$action = scalar(@argv) ? $argv[0] : 'index';	
	return ($file, $controller, $action, \@argv);
}

1;
