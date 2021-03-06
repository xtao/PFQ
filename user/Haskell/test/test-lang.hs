--  (C) 2011-14 Nicola Bonelli <nicola@pfq.io>
--
--  This program is free software; you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation; either version 2 of the License, or
--  (at your option) any later version.
--
--  This program is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU General Public License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with this program; if not, write to the Free Software Foundation,
--  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
--
--  The full GNU General Public License is included in this distribution in
--  the file called "COPYING".

import Network.PFq.Default
import Network.PFq.Experimental
import Network.PFq.Lang

import Control.Monad

-- prettyPrinter (debug):

prettyPrinter :: Serializable a => a -> IO ()
prettyPrinter comp = let (xs,_) = serialize comp 0
                 in forM_ (zip [0..] xs) $ \(n, x) -> putStrLn $ "    " ++ show n ++ ": " ++ show x

main = do
        let mycond = is_ip .&&. (is_tcp .||. is_udp)
        let mycond1 = is_udp

        let comp = par' (ip >-> udp) (ip >-> tcp) >-> steer_rtp >-> dummy 24
                    >-> conditional (mycond  .||. mycond1)
                                        steer_ip
                                        (inc 1 >-> drop')
                         >-> when' is_tcp (inc 2)  >-> dummy 11

        putStrLn "\nFunctional computation (show):"
        print comp
        putStrLn "\nFunctional computation (prettyPrint):"
        putStrLn $ pretty comp
        putStrLn "\nSerialized AST:"
        prettyPrinter comp

