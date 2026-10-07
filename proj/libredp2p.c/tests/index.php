<?php
/**
 * Summary: REDP2P index example
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);

use KaisarCode\Redp2pIndex;

require __DIR__ . '/../imp/redp2p-idx.php';

$server = new Redp2pIndex([
    'dsn' => 'sqlite:' . __DIR__ . '/redp2p.sqlite',
    'pass' => '1234'
]);
$server->serve();
