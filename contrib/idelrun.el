; Contributed by Luke Gorrie

(defun idel-region (start end)
  (interactive "r")
  (shell-command-on-region start
			   end
			   (format "idelasm | idelvm")))

(provide 'idelrun)
