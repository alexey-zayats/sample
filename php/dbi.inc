<?php

class SqlError extends Exception {
	public function __construct($message = null, $code = 0) {
		$message = $message == null ? mysql_error() : $message;
		$code = $code  == 0 ? mysql_errno() : $code;
		parent::__construct($message, $code);
	}

	public function __tostring() {
		return sprintf( "%s: [%d]: %s\n", __CLASS__, $this->code, $this->message );
	}
}

class DBI {
	private $config;
	private $handle = 0;
	private $stmt = array();
	private $counter = 0;

	private function __clone() {}
	public function __construct($server, $dbname, $username, $password) {
		$this->handle = @mysql_connect($server, $username, $password );
		if ( !$this->handle ) {
			throw new SqlError();
		}
		if( false == @mysql_select_db( $dbname, $this->handle ) ) {
			throw new SqlError();
		}
		mysql_query('SET NAMES UTF8', $this->handle );
	}

	public function __destruct() {
		$c = count($this->stmt);
		for( $i = 1; $i < $c; $i++ ) {
			$this->stmt[$i]->finish();
		}
	}

	public static function connect($server, $dbname, $username, $password = null) {
		self::$instance = new self($server, $dbname, $username, $password);
	}

	public function prepare($query) {
		$s = new Query($this->handle, $query);
		$this->stmt[] = $s;
		return $s;
	}

	public function begin() {
		if ( false == mysql_query('begin', $this->handle) ) {
			throw new SqlError();
		}
	}

	public function commit() {
		if ( false == mysql_query('commit', $this->handle) ) {
			throw new SqlError();
		}
	}

	public function rollback() {
		if ( false == mysql_query('rollback', $this->handle) ) {
			throw new SqlError();
		}
	}

	public function selectall_object () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectall_($sql, $args, 1);
	}

	public function selectall_hash () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectall_($sql, $args, 1);
	}

	public function selectall_array () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectall_($sql, $args, 0);
	}

	private function _selectall_ ($sql, $binds, $type) {
		$data = array();
		$sth = new Query($this->handle, $sql);
		foreach( $args as $k => $v ) $sth->bindval( $k+1, $v );
		$sth->execute();
		while($row = ($type == 0 ? $sth->fetchrow_array() :
						$type == 1 ? $sth->fetchrow_hash() :
						$sth->fetchrow_object() ) ) {
			array_push($data, $row);
		}
		$sth->finish();
		return $data;
	}

	public function selectrow_hash () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectrow_($sql, $args, 1);
	}

	public function selectrow_array () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectrow_($sql, $args, 0);
	}

	public function selectrow_object () {
		$args = func_get_args();
		$sql = array_shift($args);
		return $this->_selectrow_($sql, $args, 2);
	}

	private function _selectrow_ ( $sql, $binds, $type ) {
		$data = array();

		$sth = new Query($this->handle, $sql);
		foreach( $binds as $k => $v ) $sth->bindval( $k+1, $v );
		$sth->execute();

		switch($type) {
			case 0: $row = $sth->fetchrow_array(); break;
			case 1: $row = $sth->fetchrow_hash(); break;
			case 2: $row = $sth->fetchrow_object(); break;
		}

		$sth->finish();
		return $row;
	}

	public function doit () {
		$args = func_get_args();
		$sql = array_shift($args);
		$data = array();
		$sth = new Query($this->handle, $sql);
		foreach( $args as $k => $v ) $sth->bindval( $k+1, $v );
		$sth->execute();
		$sth->finish();
	}

	public function last_id() {
		return mysql_insert_id($this->handle);
	}
}

class Query {
	private $handle = null;
	private $query = null;
	private $result = 0;
	private $binds = array();
	private $clear = 1;
	private $select = 0;

	public function __construct($handle, $query) {
		$this->handle = $handle;
		$this->query = $query;
		$this->select = preg_match( "/select/i", $query );
	}

	public function execute() {
		$binds = func_get_args();
		$query = $this->query;
		foreach( $binds as $holder => $value ) {
			$index = $holder + 1;
			if ( is_array($value) ) {
				for( $i = 0; $i < count($value); $i++ ) {
					$value[$i] = mysql_escape_string($value[$i]);
				}
				$query = str_replace(':'.$index.':', "'".implode("','",$value)."'", $query );
			} else {
				$query = str_replace(':'.$index.':', "'".mysql_escape_string($value)."'", $query);
			}
		}
		if ( _DEBUG_ > 0 ) {
			echo $query.'<br/>';
		}
		if ( _DEBUG_ > 1 && $this->select ) {
			$res = mysql_query( 'EXPLAIN '.$query, $this->handle );
			echo '<table border="1" cellspacing="0" cellpadding="0">';
			echo '<tr bgcolor="#eaeaea">';
			for( $i = 0; $i < mysql_num_fields($res); $i++ ) {
				echo '<th>'.mysql_field_name($res, $i).'</th>';
			}
			echo '</tr>';
			while ( $row = mysql_fetch_array( $res, MYSQL_ASSOC ) ) {
				echo '<tr>';
				foreach ( $row as $key => $value ) {
					echo '<td>'.($value ? $value : '&nbsp;').'</td>';
				}
				echo '</tr>';
			}
			echo '</table>';
			mysql_free_result($res);
		}
		$this->result = mysql_query( $query );
		if ( $this->result == false ) {
			throw new SqlError( sprintf('error(%s); query(%s)', mysql_error(), $query) );
		}
		$this->clear = 0;
	}

	public function bindval($index,$value) {
		$query = $this->query;
		if ( is_array($value) ) {
			for( $i = 0; $i < count($value); $i++ ) {
				$value[$i] = mysql_escape_string($value[$i]);
			}
			$query = str_replace(':'.$index.':', "'".implode("','",$value)."'", $query );
		} else {
			$query = str_replace(':'.$index.':', "'".mysql_escape_string($value)."'", $query);
		}
		$this->query = $query;
	}

	public function rows() {
		return $this->select ? mysql_num_rows($this->result) : mysql_affected_rows($this->result);
	}

	public function fetchrow_hash() {
		return mysql_fetch_array( $this->result, MYSQL_ASSOC );
	}

	public function fetchrow_array() {
		return mysql_fetch_array( $this->result, MYSQL_NUM );
	}

	public function fetchrow_object() {
		return mysql_fetch_object( $this->result );
	}

	public function finish() {
		if ( $this->select && $this->result ) {
			mysql_free_result( $this->result );
			$this->result = 0;
		}
	}
}



?>
